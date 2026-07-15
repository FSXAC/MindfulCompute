#include "OverlayWindow.h"
#include "Theme.h"
#include "Log.h"
#include <windowsx.h>
#include <shellscalingapi.h>
#include <wtsapi32.h>
#include <cmath>
#include <algorithm>
#include <cstdio>
#include <cwchar>

namespace {
const wchar_t* kClass = L"MindfulOverlayWindow";
constexpr int  kIntentionId = 1500;
constexpr int  kReflectionId = 1501;

constexpr UINT_PTR TIMER_FRAME     = 10;
constexpr UINT_PTR TIMER_LOGIC     = 11;
constexpr UINT_PTR TIMER_AUTOFOCUS = 12;
constexpr UINT_PTR TIMER_BREAKSHOW = 13;
constexpr UINT_PTR TIMER_DUMP      = 14;   // QA frame dumps (MINDFUL_DUMP_DIR)

const wchar_t* screenTag(int s) {
    switch (s) {
        case 0: return L"idle";     case 1: return L"titleenter";
        case 2: return L"titleshow";case 3: return L"titlerelease";
        case 4: return L"hidden";   case 5: return L"breakgather";
        case 6: return L"break";    default: return L"x";
    }
}

double easeInOut(double t) {
    if (t < 0) t = 0; if (t > 1) t = 1;
    return t < 0.5 ? 2 * t * t : 1 - std::pow(-2 * t + 2, 2) / 2;
}
float lerp(float a, float b, float t) { return a + (b - a) * t; }
} // namespace

// ----------------------------------------------------------------- Tween ----
void Tween::go(double target, double durMs, ULONGLONG now) {
    from = cur; to = target; dur = durMs; start = now; active = (durMs > 0);
    if (!active) cur = target;
}
bool Tween::tick(ULONGLONG now) {
    if (!active) return false;
    double t = (dur <= 0) ? 1.0 : double(now - start) / dur;
    if (t >= 1.0) { cur = to; active = false; return false; }
    cur = from + (to - from) * easeInOut(t);
    return true;
}

// ---------------------------------------------------------------- create ----
bool OverlayWindow::create(HINSTANCE hInst, GraphicsDevice* gfx, PageController* page,
                           const RECT& workArea, UINT dpi) {
    gfx_ = gfx; page_ = page; work_ = workArea; dpi_ = dpi; scale_ = dpi / 96.f;

    BOOL anim = TRUE;
    SystemParametersInfoW(SPI_GETCLIENTAREAANIMATION, 0, &anim, 0);
    reduceMotion_ = (anim == FALSE);
    // Dev override so the animated path can be exercised on a machine that has
    // reduce-motion enabled system-wide (MINDFUL_FORCE_MOTION=1 forces motion,
    // =0 forces reduced). Absent -> honour the system setting.
    if (GetEnvironmentVariableW(L"MINDFUL_FORCE_MOTION", nullptr, 0) > 0) {
        wchar_t v[8]{}; GetEnvironmentVariableW(L"MINDFUL_FORCE_MOTION", v, 8);
        reduceMotion_ = (v[0] == L'0');
    }
    Log::write(L"[overlay] dpi=%u scale=%.2f reduceMotion=%d", dpi_, scale_, reduceMotion_ ? 1 : 0);

    // QA: periodic PNG dumps of what's actually drawn (headless sessions can't
    // screen-capture). MINDFUL_DUMP_DIR=<dir> enables it.
    wchar_t dbuf[512]{};
    DWORD dn = GetEnvironmentVariableW(L"MINDFUL_DUMP_DIR", dbuf, 512);
    if (dn > 0 && dn < 512) {
        dumpDir_ = dbuf;
        CreateDirectoryW(dumpDir_.c_str(), nullptr);
        Log::write(L"[overlay] MINDFUL_DUMP_DIR=%ls -> frame dumps enabled", dumpDir_.c_str());
    }

    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = &OverlayWindow::WndProc;
    wc.hInstance = hInst;
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wc.lpszClassName = kClass;
    RegisterClassExW(&wc);

    const int w = work_.right - work_.left;
    const int h = work_.bottom - work_.top;
    const DWORD exStyle = WS_EX_TOOLWINDOW | WS_EX_NOREDIRECTIONBITMAP | WS_EX_TOPMOST;
    hwnd_ = CreateWindowExW(exStyle, kClass, L"MindfulCompute", WS_POPUP,
                            work_.left, work_.top, w, h, nullptr, nullptr, hInst, this);
    if (!hwnd_) { Log::write(L"[overlay] CreateWindowEx failed err=%lu", GetLastError()); return false; }

    if (!comp_.init(gfx_, hwnd_, (UINT)w, (UINT)h, dpi_)) {
        Log::write(L"[overlay] composition init failed"); return false;
    }
    tl_.init(gfx_->dwrite());
    nudge_.set(1.0);   // 1.0 == no scale (pulse briefly bumps to 1.03)

    // Drive transitions off phase changes; forward to main for the tray.
    page_->onPhaseChanged = [this](Phase p) { onPhase(p); if (onPhaseObserved) onPhaseObserved(p); };

    computeLayout();
    int fontPx = (int)std::lround(Theme::kFntField * scale_);

    intentionField_.create(hwnd_, hInst, kIntentionId, toScreen(fieldRect_), fontPx);
    intentionField_.onSubmit = [this]() { if (page_->canBegin()) page_->begin(); };
    intentionField_.onEscape = [this]() { if (onQuitRequested) onQuitRequested(); };
    intentionField_.onChange = [this]() { page_->setIntentionRaw(intentionField_.text()); invalidate(); };
    intentionField_.onFocusChanged = [this](bool) { invalidate(); };

    reflectionField_.create(hwnd_, hInst, kReflectionId, toScreen(fieldRect_), fontPx);
    reflectionField_.onSubmit = [this]() { page_->continueFromBreak(); };
    reflectionField_.onEscape = [this]() { if (onQuitRequested) onQuitRequested(); };
    reflectionField_.onChange = [this]() { page_->setReflectionRaw(reflectionField_.text()); };

    if (!dumpDir_.empty()) SetTimer(hwnd_, TIMER_DUMP, 850, nullptr);

    LONG_PTR exs = GetWindowLongPtrW(hwnd_, GWL_EXSTYLE);
    Log::write(L"[overlay] created hwnd=0x%p %dx%d at (%ld,%ld) EXSTYLE=0x%08llX", (void*)hwnd_, w, h,
               work_.left, work_.top, (unsigned long long)exs);
    return true;
}

// ------------------------------------------------------------ coordinate ----
RECT OverlayWindow::toScreen(const D2D1_RECT_F& dip) const {
    return RECT{
        work_.left + (LONG)std::lround(dip.left  * scale_),
        work_.top  + (LONG)std::lround(dip.top   * scale_),
        work_.left + (LONG)std::lround(dip.right * scale_),
        work_.top  + (LONG)std::lround(dip.bottom * scale_),
    };
}
void OverlayWindow::positionField(TextField& f, const D2D1_RECT_F& dip) {
    f.setScreenRect(toScreen(dip));
}

// --------------------------------------------------------------- layout -----
void OverlayWindow::computeLayout() {
    const float W = comp_.dipWidth();
    const float H = comp_.dipHeight();
    const float PAD = Theme::kPanelPad;
    const float CW = Theme::kPanelWidth - 2 * PAD;
    const float px = (W - Theme::kPanelWidth) * 0.5f;

    auto sansH = [&](float sz) { return sz * 1.34f; };  // single-line height est.

    if (page_->phase() == Phase::Resting) {
        // ---- break panel (taller; content fixed while resting) ----
        TextStyle sEcho{ Theme::kSans, Theme::kFntGreeting, DWRITE_FONT_WEIGHT_NORMAL,
                         DWRITE_FONT_STYLE_NORMAL, DWRITE_TEXT_ALIGNMENT_CENTER };
        TextStyle sQuote{ Theme::kSerif, Theme::kFntQuote, DWRITE_FONT_WEIGHT_NORMAL,
                          DWRITE_FONT_STYLE_ITALIC, DWRITE_TEXT_ALIGNMENT_CENTER };
        sQuote.lineSpacing = 3.f;

        std::wstring intention = page_->intention();
        std::wstring echo = L"You set out to: “" + intention + L"”";
        const Quote& q = page_->quote();

        float y = PAD;
        y += 24.f;                       // breathing glyph region
        y += 14.f;                       // VStack spacing 14
        y += sansH(Theme::kFntEyebrow);  // eyebrow
        y += 10.f;
        y += sansH(Theme::kFntTitle);    // "Time to step away"
        if (!intention.empty()) {
            y += 14.f;
            y += tl_.measureHeight(echo, sEcho, CW);
        }
        y += 24.f;
        y += tl_.measureHeight(q.text, sQuote, CW - 16.f);
        if (!q.author.empty()) y += 8.f + sansH(Theme::kFntCaption);
        y += 24.f;
        y += sansH(Theme::kFntCallout);  // "Before you go..."
        y += 8.f;
        float reflY = y;
        y += Theme::kFieldHeight;
        y += 24.f;
        float contY = y;
        y += Theme::kButtonHeight;
        float panelH = y + PAD;

        float py = std::max(8.f, (H - panelH) * 0.5f - H * 0.04f);
        panelRect_ = D2D1::RectF(px, py, px + Theme::kPanelWidth, py + panelH);
        reflectRect_ = D2D1::RectF(px + PAD, py + reflY, px + PAD + CW, py + reflY + Theme::kFieldHeight);
        continueRect_ = D2D1::RectF(px + PAD, py + contY, px + PAD + CW, py + contY + Theme::kButtonHeight);
    } else {
        // ---- start / idle panel (fixed height) ----
        float y = PAD;
        y += sansH(Theme::kFntGreeting);         // greeting
        y += 8.f;
        y += sansH(Theme::kFntTitle);            // "What are you here to do?"
        y += 24.f;
        float fieldY = y;
        y += Theme::kFieldHeight;
        y += 24.f;
        y += sansH(Theme::kFntCallout);          // "For N minutes"
        y += 10.f;
        float sliderY = y;
        y += 22.f;                               // slider
        y += 6.f;
        y += sansH(Theme::kFntCaption2);         // 5 min / 1 hour
        y += 24.f;
        float beginY = y;
        y += Theme::kButtonHeight;
        y += 10.f;
        y += sansH(Theme::kFntCaption2);         // version
        float panelH = y + PAD - 12.f;

        float py = std::max(8.f, (H - panelH) * 0.5f - H * 0.05f);
        panelRect_ = D2D1::RectF(px, py, px + Theme::kPanelWidth, py + panelH);
        fieldRect_ = D2D1::RectF(px + PAD, py + fieldY, px + PAD + CW, py + fieldY + Theme::kFieldHeight);
        sliderRect_ = D2D1::RectF(px + PAD, py + sliderY, px + PAD + CW, py + sliderY + 22.f);
        beginRect_ = D2D1::RectF(px + PAD, py + beginY, px + PAD + CW, py + beginY + Theme::kButtonHeight);
    }
}

// ---------------------------------------------------------- state changes ---
void OverlayWindow::onPhase(Phase p) {
    switch (p) {
    case Phase::Running: enterTitle(); break;
    case Phase::Resting: enterBreakGather(); break;
    case Phase::Idle:    enterIdle(false); break;
    }
}

void OverlayWindow::show() { enterIdle(true); }

void OverlayWindow::enterIdle(bool firstShow) {
    ULONGLONG now = GetTickCount64();
    screen_ = Screen::Idle;
    reflectionField_.hide();
    KillTimer(hwnd_, TIMER_LOGIC);   // idle: no session running

    ShowWindow(hwnd_, SW_SHOWNA);
    SetWindowPos(hwnd_, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);

    computeLayout();
    if (firstShow) {
        dim_.set(0);
        dim_.go(Theme::kDimRest, Theme::kDimGatherMs, now);   // gather ~2s around panel
    } else {
        dim_.set(Theme::kDimRest);                            // continue-after-break: already dim
    }
    intentionField_.setText(page_->intention());   // cleared after a break
    positionField(intentionField_, fieldRect_);
    SetTimer(hwnd_, TIMER_AUTOFOCUS, (UINT)Theme::kAutofocusMs, nullptr);
    startFrames();
    invalidate();
    Log::write(L"[overlay] -> Idle (firstShow=%d)", firstShow ? 1 : 0);
}

void OverlayWindow::enterTitle() {
    ULONGLONG now = GetTickCount64();
    screen_ = Screen::TitleShow;
    titleStart_ = now;
    intentionField_.hide();
    KillTimer(hwnd_, TIMER_AUTOFOCUS);
    SetTimer(hwnd_, TIMER_LOGIC, 200, nullptr);   // poll the mock session deadline
    dim_.go(Theme::kDimDeep, Theme::kDimDeepenMs, now);  // deepen; panel gone, no bright flash
    card_.set(0);
    card_.go(1.0, Theme::kCardFadeInMs, now);   // card fades in over ~1.2s (matches OverlayView)
    startFrames();
    invalidate();
    Log::write(L"[overlay] -> TitleShow");
}

void OverlayWindow::enterHidden() {
    screen_ = Screen::Hidden;
    ShowWindow(hwnd_, SW_HIDE);
    if (onDimChanged) onDimChanged(0.f, false);   // release the dumb dimmers too
    Log::write(L"[overlay] -> Hidden (session running, desktop returned)");
}

void OverlayWindow::enterBreakGather() {
    ULONGLONG now = GetTickCount64();
    screen_ = Screen::BreakGather;
    KillTimer(hwnd_, TIMER_BREAKSHOW);
    ShowWindow(hwnd_, SW_SHOWNA);
    SetWindowPos(hwnd_, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
    SetTimer(hwnd_, TIMER_LOGIC, 200, nullptr);   // resting: keep polling (auto-dismiss)
    card_.set(0);
    dim_.set(0);
    dim_.go(Theme::kDimRest, Theme::kDimBreakMs, now);   // dim gathers first, panel announced by it
    SetTimer(hwnd_, TIMER_BREAKSHOW, (UINT)Theme::kBreakDelayMs, nullptr);
    startFrames();
    invalidate();
    Log::write(L"[overlay] -> BreakGather");
}

void OverlayWindow::enterBreak() {
    ULONGLONG now = GetTickCount64();
    screen_ = Screen::Break;
    breakStart_ = now;
    computeLayout();
    reflectionField_.setText(L"");
    positionField(reflectionField_, reflectRect_);
    reflectionField_.showAndFocus();
    startFrames();
    invalidate();
    Log::write(L"[overlay] -> Break");
}

// ------------------------------------------------------------ interaction ---
void OverlayWindow::pulse() {
    if (screen_ != Screen::Idle && screen_ != Screen::Break) return;
    ULONGLONG now = GetTickCount64();
    nudge_.set(1.0);
    nudge_.go(1.03, Theme::kNudgeSwellMs, now);   // swell to 1.03 (~0.22s, matches PanelRoot spring)
    // second leg (settle over ~0.45s) scheduled by the tick when the first completes
    startFrames();
}

void OverlayWindow::bringToFront() {
    SetWindowPos(hwnd_, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
    if (screen_ == Screen::Idle) intentionField_.showAndFocus();
    else if (screen_ == Screen::Break) reflectionField_.showAndFocus();
}

int OverlayWindow::sliderValueFromX(float xDip) const {
    float t = (xDip - sliderRect_.left) / std::max(1.f, sliderRect_.right - sliderRect_.left);
    t = std::clamp(t, 0.f, 1.f);
    int raw = (int)std::lround(5 + t * 55);
    int snapped = ((raw + 2) / 5) * 5;
    return std::clamp(snapped, 5, 60);
}

// ----------------------------------------------------------------- frames ---
bool OverlayWindow::animating() const {
    if (dim_.active || card_.active || nudge_.active) return true;
    if (screen_ == Screen::TitleShow) return true;   // breathing guide
    if (screen_ == Screen::Break) return true;        // ember glyph breathes
    return false;
}
void OverlayWindow::startFrames() { SetTimer(hwnd_, TIMER_FRAME, 16, nullptr); }
void OverlayWindow::stopFramesIfIdle() { if (!animating()) KillTimer(hwnd_, TIMER_FRAME); }
void OverlayWindow::invalidate() { render(); }

// ----------------------------------------------------------------- render ---
void OverlayWindow::render() {
    if (screen_ == Screen::Hidden) return;
    ID2D1DeviceContext* dc = comp_.begin();
    dc->Clear(D2D1::ColorF(0, 0, 0, 0));
    if (!brush_) dc->CreateSolidColorBrush(D2D1::ColorF(1, 1, 1, 1), &brush_);

    // 1) dim over the whole work area
    brush_->SetColor(Theme::dimColor((float)dim_.cur));
    dc->FillRectangle(D2D1::RectF(0, 0, comp_.dipWidth(), comp_.dipHeight()), brush_.Get());
    if (onDimChanged) onDimChanged((float)dim_.cur, true);   // dumb dimmers follow

    // 2) content by draw order
    switch (screen_) {
    case Screen::Idle:  drawStart(dc, brush_.Get()); break;
    case Screen::Break: drawBreak(dc, brush_.Get()); break;
    case Screen::TitleShow:
    case Screen::TitleEnter:
    case Screen::TitleRelease: drawTitleCard(dc, brush_.Get()); break;
    default: break;   // BreakGather: only the dim shows (panel announced by it)
    }
    comp_.end();
}

void OverlayWindow::drawCardChrome(ID2D1DeviceContext* dc, ID2D1SolidColorBrush* b, D2D1_COLOR_F accent) {
    // Optional pulse: scale the whole card about its centre.
    D2D1_MATRIX_3X2_F saved; dc->GetTransform(&saved);
    if (nudge_.cur != 1.0) {
        float cx = (panelRect_.left + panelRect_.right) * 0.5f;
        float cy = (panelRect_.top + panelRect_.bottom) * 0.5f;
        dc->SetTransform(D2D1::Matrix3x2F::Scale((float)nudge_.cur, (float)nudge_.cur, D2D1::Point2F(cx, cy)) * saved);
    }
    D2D1_ROUNDED_RECT rr{ panelRect_, Theme::kPanelRadius, Theme::kPanelRadius };
    b->SetColor(Theme::panelFill());
    dc->FillRoundedRectangle(rr, b);

    // subtle accent wash top->bottom, clipped to the card
    ComPtr<ID2D1GradientStopCollection> stops;
    D2D1_GRADIENT_STOP gs[2] = {
        { 0.f, D2D1::ColorF(accent.r, accent.g, accent.b, 0.10f) },
        { 1.f, D2D1::ColorF(accent.r, accent.g, accent.b, 0.03f) },
    };
    if (SUCCEEDED(dc->CreateGradientStopCollection(gs, 2, &stops))) {
        ComPtr<ID2D1LinearGradientBrush> lg;
        D2D1_LINEAR_GRADIENT_BRUSH_PROPERTIES lp{ D2D1::Point2F(panelRect_.left, panelRect_.top),
                                                  D2D1::Point2F(panelRect_.left, panelRect_.bottom) };
        if (SUCCEEDED(dc->CreateLinearGradientBrush(lp, stops.Get(), &lg))) {
            dc->PushAxisAlignedClip(panelRect_, D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
            dc->FillRoundedRectangle(rr, lg.Get());
            dc->PopAxisAlignedClip();
        }
    }
    b->SetColor(Theme::panelBorder());
    dc->DrawRoundedRectangle(rr, b, 1.f);
    // transform stays applied for the caller's content; restored by caller
    (void)saved;
}

void OverlayWindow::drawStart(ID2D1DeviceContext* dc, ID2D1SolidColorBrush* b) {
    D2D1_MATRIX_3X2_F saved; dc->GetTransform(&saved);
    drawCardChrome(dc, b, Theme::sage());

    const float PAD = Theme::kPanelPad;
    const float L = panelRect_.left + PAD;
    const float R = panelRect_.right - PAD;
    const float CW = R - L;
    auto sansH = [&](float sz) { return sz * 1.34f; };
    float y = panelRect_.top + PAD;

    // greeting (time of day)
    SYSTEMTIME st; GetLocalTime(&st);
    const wchar_t* greeting = (st.wHour >= 5 && st.wHour < 12) ? L"Good morning"
                            : (st.wHour >= 12 && st.wHour < 17) ? L"Good afternoon"
                            : L"Good evening";
    TextStyle sFoot{ Theme::kSans, Theme::kFntGreeting };
    b->SetColor(Theme::txtSecondary());
    tl_.draw(dc, greeting, sFoot, D2D1::RectF(L, y, R, y + sansH(Theme::kFntGreeting)), b);
    y += sansH(Theme::kFntGreeting) + 8.f;

    TextStyle sTitle{ Theme::kSerif, Theme::kFntTitle, DWRITE_FONT_WEIGHT_MEDIUM };
    b->SetColor(Theme::txtPrimary());
    tl_.draw(dc, L"What are you here to do?", sTitle, D2D1::RectF(L, y, R, y + sansH(Theme::kFntTitle) + 6), b);
    y += sansH(Theme::kFntTitle) + 24.f;

    // field chrome / focus ring (the EDIT host floats above; ring drawn outside it)
    D2D1_ROUNDED_RECT fr{ fieldRect_, Theme::kFieldRadius, Theme::kFieldRadius };
    b->SetColor(Theme::fieldFill());
    dc->FillRoundedRectangle(fr, b);
    if (intentionField_.focused()) {
        D2D1_RECT_F ring = D2D1::RectF(fieldRect_.left - 1, fieldRect_.top - 1,
                                       fieldRect_.right + 1, fieldRect_.bottom + 1);
        D2D1_ROUNDED_RECT rr{ ring, Theme::kFieldRadius + 1, Theme::kFieldRadius + 1 };
        b->SetColor(Theme::sage(0.6f));
        dc->DrawRoundedRectangle(rr, b, 1.5f);
    } else {
        b->SetColor(Theme::fieldBorder());
        dc->DrawRoundedRectangle(fr, b, 1.f);
    }
    y = fieldRect_.bottom + 24.f;

    // "For N minutes"
    wchar_t forLbl[64];
    int mins = page_->minutes();
    swprintf_s(forLbl, L"For %d minutes", mins);
    // draw "For " normal then value semibold: approximate with one run, semibold value
    TextStyle sCallout{ Theme::kSans, Theme::kFntCallout };
    b->SetColor(Theme::txtPrimary());
    {
        auto lay = tl_.layout(forLbl, sCallout, CW, sansH(Theme::kFntCallout) + 4);
        if (lay) {
            DWRITE_TEXT_RANGE valRange{ 4, (UINT32)wcslen(forLbl) - 4 };
            lay->SetFontWeight(DWRITE_FONT_WEIGHT_SEMI_BOLD, valRange);
            dc->DrawTextLayout(D2D1::Point2F(L, y), lay.Get(), b);
        }
    }
    y += sansH(Theme::kFntCallout) + 10.f;

    // slider: track, sage fill, knob
    float trackY = sliderRect_.top + 11.f;
    float th = 3.f;
    D2D1_ROUNDED_RECT track{ D2D1::RectF(L, trackY - th / 2, R, trackY + th / 2), th / 2, th / 2 };
    b->SetColor(Theme::white(0.18f));
    dc->FillRoundedRectangle(track, b);
    float t = (mins - 5) / 55.f;
    float knobX = L + t * CW;
    D2D1_ROUNDED_RECT fill{ D2D1::RectF(L, trackY - th / 2, knobX, trackY + th / 2), th / 2, th / 2 };
    b->SetColor(Theme::sage());
    dc->FillRoundedRectangle(fill, b);
    b->SetColor(Theme::white(0.95f));
    dc->FillEllipse(D2D1::Ellipse(D2D1::Point2F(knobX, trackY), 8.f, 8.f), b);
    b->SetColor(Theme::sage());
    dc->FillEllipse(D2D1::Ellipse(D2D1::Point2F(knobX, trackY), 4.f, 4.f), b);
    y = sliderRect_.bottom + 6.f;

    // 5 min / 1 hour
    TextStyle sMin{ Theme::kSans, Theme::kFntCaption2, DWRITE_FONT_WEIGHT_NORMAL,
                    DWRITE_FONT_STYLE_NORMAL, DWRITE_TEXT_ALIGNMENT_LEADING };
    TextStyle sMax = sMin; sMax.align = DWRITE_TEXT_ALIGNMENT_TRAILING;
    b->SetColor(Theme::txtTertiary());
    tl_.draw(dc, L"5 min", sMin, D2D1::RectF(L, y, R, y + sansH(Theme::kFntCaption2)), b);
    tl_.draw(dc, L"1 hour", sMax, D2D1::RectF(L, y, R, y + sansH(Theme::kFntCaption2)), b);
    y += sansH(Theme::kFntCaption2) + 24.f;

    // Begin button (sage; disabled until intention non-empty)
    bool enabled = page_->canBegin();
    D2D1_ROUNDED_RECT btn{ beginRect_, Theme::kButtonRadius, Theme::kButtonRadius };
    b->SetColor(Theme::sage(enabled ? 1.f : 0.35f));
    dc->FillRoundedRectangle(btn, b);
    TextStyle sBtn{ Theme::kSans, Theme::kFntButton, DWRITE_FONT_WEIGHT_SEMI_BOLD,
                    DWRITE_FONT_STYLE_NORMAL, DWRITE_TEXT_ALIGNMENT_CENTER };
    float bh = beginRect_.bottom - beginRect_.top;
    float th2 = sansH(Theme::kFntButton);
    b->SetColor(Theme::white(enabled ? 0.98f : 0.5f));
    tl_.draw(dc, L"Begin", sBtn, D2D1::RectF(beginRect_.left, beginRect_.top + (bh - th2) / 2,
                                             beginRect_.right, beginRect_.bottom), b);

    // version bottom-right -- version + timestamped build number, matching the
    // macOS AppVersion.display ("v1.2.2-win (260714.2310)").
    std::wstring ver = L"v" MINDFUL_VERSION L" (" MINDFUL_BUILD_STAMP L")";
    TextStyle sVer{ Theme::kSans, Theme::kFntCaption2, DWRITE_FONT_WEIGHT_NORMAL,
                    DWRITE_FONT_STYLE_NORMAL, DWRITE_TEXT_ALIGNMENT_TRAILING };
    float vy = beginRect_.bottom + 10.f;
    b->SetColor(Theme::txtQuaternary());
    tl_.draw(dc, ver, sVer, D2D1::RectF(L, vy, R, vy + sansH(Theme::kFntCaption2)), b);

    dc->SetTransform(saved);
}

void OverlayWindow::drawBreak(ID2D1DeviceContext* dc, ID2D1SolidColorBrush* b) {
    D2D1_MATRIX_3X2_F saved; dc->GetTransform(&saved);
    drawCardChrome(dc, b, Theme::ember());

    const float PAD = Theme::kPanelPad;
    const float L = panelRect_.left + PAD;
    const float R = panelRect_.right - PAD;
    const float CW = R - L;
    const float midX = (panelRect_.left + panelRect_.right) * 0.5f;
    auto sansH = [&](float sz) { return sz * 1.34f; };
    float y = panelRect_.top + PAD;

    // ember breathing glyph
    drawBreathGuide(dc, b, midX, y + 12.f, -1.f);   // -1 => small ember dot variant
    y += 24.f + 14.f;

    // eyebrow "N MINUTES LATER"
    wchar_t eyebrow[48];
    int u = page_->completedUnits();
    swprintf_s(eyebrow, L"%d MINUTES LATER", u);
    if (u == 1) wcscpy_s(eyebrow, L"1 MINUTE LATER");
    TextStyle sEye{ Theme::kSans, Theme::kFntEyebrow, DWRITE_FONT_WEIGHT_SEMI_BOLD,
                    DWRITE_FONT_STYLE_NORMAL, DWRITE_TEXT_ALIGNMENT_CENTER };
    sEye.tracking = 4.f;
    b->SetColor(Theme::ember());
    tl_.draw(dc, eyebrow, sEye, D2D1::RectF(L, y, R, y + sansH(Theme::kFntEyebrow)), b);
    y += sansH(Theme::kFntEyebrow) + 10.f;

    TextStyle sTitle{ Theme::kSerif, Theme::kFntTitle, DWRITE_FONT_WEIGHT_MEDIUM,
                      DWRITE_FONT_STYLE_NORMAL, DWRITE_TEXT_ALIGNMENT_CENTER };
    b->SetColor(Theme::txtPrimary());
    tl_.draw(dc, L"Time to step away", sTitle, D2D1::RectF(L, y, R, y + sansH(Theme::kFntTitle) + 6), b);
    y += sansH(Theme::kFntTitle) + 14.f;

    std::wstring intention = page_->intention();
    if (!intention.empty()) {
        std::wstring echo = L"You set out to: “" + intention + L"”";
        TextStyle sEcho{ Theme::kSans, Theme::kFntGreeting, DWRITE_FONT_WEIGHT_NORMAL,
                         DWRITE_FONT_STYLE_NORMAL, DWRITE_TEXT_ALIGNMENT_CENTER };
        float hh = tl_.measureHeight(echo, sEcho, CW);
        b->SetColor(Theme::txtSecondary());
        tl_.draw(dc, echo, sEcho, D2D1::RectF(L, y, R, y + hh), b);
        y += hh;
    }
    y += 24.f;

    // quote + author
    const Quote& q = page_->quote();
    TextStyle sQuote{ Theme::kSerif, Theme::kFntQuote, DWRITE_FONT_WEIGHT_NORMAL,
                      DWRITE_FONT_STYLE_ITALIC, DWRITE_TEXT_ALIGNMENT_CENTER };
    sQuote.lineSpacing = 3.f;
    float qh = tl_.measureHeight(q.text, sQuote, CW - 16.f);
    b->SetColor(Theme::txtPrimary(0.92f));
    tl_.draw(dc, q.text, sQuote, D2D1::RectF(L + 8, y, R - 8, y + qh), b);
    y += qh;
    if (!q.author.empty()) {
        y += 8.f;
        std::wstring auth = L"— " + q.author;
        TextStyle sAuth{ Theme::kSans, Theme::kFntCaption, DWRITE_FONT_WEIGHT_NORMAL,
                         DWRITE_FONT_STYLE_NORMAL, DWRITE_TEXT_ALIGNMENT_CENTER };
        b->SetColor(Theme::txtTertiary());
        tl_.draw(dc, auth, sAuth, D2D1::RectF(L, y, R, y + sansH(Theme::kFntCaption)), b);
        y += sansH(Theme::kFntCaption);
    }
    y += 24.f;

    // "Before you go — how did it go?"
    TextStyle sPrompt{ Theme::kSans, Theme::kFntCallout };
    b->SetColor(Theme::txtSecondary());
    tl_.draw(dc, L"Before you go — how did it go?", sPrompt,
             D2D1::RectF(L, y, R, y + sansH(Theme::kFntCallout)), b);

    // reflection field chrome (host floats above)
    D2D1_ROUNDED_RECT rf{ reflectRect_, Theme::kFieldRadius, Theme::kFieldRadius };
    b->SetColor(Theme::fieldFill());
    dc->FillRoundedRectangle(rf, b);
    b->SetColor(Theme::fieldBorder());
    dc->DrawRoundedRectangle(rf, b, 1.f);

    // Continue button (ember)
    D2D1_ROUNDED_RECT btn{ continueRect_, Theme::kButtonRadius, Theme::kButtonRadius };
    b->SetColor(Theme::ember());
    dc->FillRoundedRectangle(btn, b);
    TextStyle sBtn{ Theme::kSans, Theme::kFntButton, DWRITE_FONT_WEIGHT_SEMI_BOLD,
                    DWRITE_FONT_STYLE_NORMAL, DWRITE_TEXT_ALIGNMENT_CENTER };
    float bh = continueRect_.bottom - continueRect_.top;
    float th2 = sansH(Theme::kFntButton);
    b->SetColor(Theme::white(0.98f));
    tl_.draw(dc, L"Continue", sBtn, D2D1::RectF(continueRect_.left, continueRect_.top + (bh - th2) / 2,
                                                continueRect_.right, continueRect_.bottom), b);
    dc->SetTransform(saved);
}

// ------------------------------------------------------------- breathing ----
void OverlayWindow::drawBreathGuide(ID2D1DeviceContext* dc, ID2D1SolidColorBrush* b,
                                    float cx, float cy, float opacity) {
    ULONGLONG now = GetTickCount64();

    if (opacity < 0.f) {
        // Small ember dot (break panel). Slow sinusoidal breath (period 8s).
        double u = reduceMotion_ ? 0.5 : (std::sin(2 * 3.14159265 * double(now - breakStart_) / 8000.0 - 3.14159265 / 2) + 1) / 2;
        float scale = lerp(0.8f, 1.3f, (float)u);
        float alpha = lerp(0.55f, 0.95f, (float)u);
        float r = 7.f * scale;
        // soft glow
        b->SetColor(Theme::ember(0.28f * alpha));
        dc->FillEllipse(D2D1::Ellipse(D2D1::Point2F(cx, cy), r + 4, r + 4), b);
        b->SetColor(Theme::ember(alpha));
        dc->FillEllipse(D2D1::Ellipse(D2D1::Point2F(cx, cy), r, r), b);
        return;
    }

    // Title-card box-breathing guide (halo + ring + dot + label).
    double s;   // 0 exhaled .. 1 inhaled
    const wchar_t* label;
    if (reduceMotion_) { s = 0.5; label = L"Take a slow breath"; }
    else {
        double t = std::fmod(double(now - titleStart_), Theme::kBreathCycleMs) / 1000.0; // 0..12
        if (t < 4)      { s = easeInOut(t / 4);        label = L"Breathe in"; }
        else if (t < 8) { s = 1.0;                     label = L"Hold"; }
        else            { s = 1.0 - easeInOut((t - 8) / 4); label = L"Breathe out"; }
    }
    float base = Theme::kHaloBase / 2.f;   // radius
    float haloR = base * lerp(0.65f, 1.55f, (float)s);
    float ringR = base * lerp(0.60f, 1.40f, (float)s);
    float dotR  = (Theme::kDotBase / 2.f) * lerp(0.75f, 1.25f, (float)s);

    // soft halo via radial gradient (approximates the SwiftUI blur)
    ComPtr<ID2D1GradientStopCollection> stops;
    D2D1_GRADIENT_STOP gs[2] = {
        { 0.f, Theme::white(0.22f * opacity) },
        { 1.f, Theme::white(0.f) },
    };
    if (SUCCEEDED(dc->CreateGradientStopCollection(gs, 2, &stops))) {
        ComPtr<ID2D1RadialGradientBrush> rg;
        D2D1_RADIAL_GRADIENT_BRUSH_PROPERTIES rp{ D2D1::Point2F(cx, cy), D2D1::Point2F(0, 0), haloR, haloR };
        if (SUCCEEDED(dc->CreateRadialGradientBrush(rp, stops.Get(), &rg)))
            dc->FillEllipse(D2D1::Ellipse(D2D1::Point2F(cx, cy), haloR, haloR), rg.Get());
    }
    b->SetColor(Theme::white(0.9f * opacity));
    dc->DrawEllipse(D2D1::Ellipse(D2D1::Point2F(cx, cy), ringR, ringR), b, 2.5f);
    dc->FillEllipse(D2D1::Ellipse(D2D1::Point2F(cx, cy), dotR, dotR), b);

    // label below the guide box
    TextStyle sLbl{ Theme::kSans, Theme::kFntBreath, DWRITE_FONT_WEIGHT_MEDIUM,
                    DWRITE_FONT_STYLE_NORMAL, DWRITE_TEXT_ALIGNMENT_CENTER };
    sLbl.tracking = 2.5f;
    float ly = cy + Theme::kGuideBox / 2.f + 4.f;
    b->SetColor(Theme::white(0.85f * opacity));
    tl_.draw(dc, label, sLbl, D2D1::RectF(cx - 200, ly, cx + 200, ly + Theme::kFntBreath * 1.4f), b);
}

void OverlayWindow::drawTitleCard(ID2D1DeviceContext* dc, ID2D1SolidColorBrush* b) {
    const float W = comp_.dipWidth();
    const float H = comp_.dipHeight();
    const float op = (float)card_.cur;
    if (op <= 0.001f) return;

    int mins = page_->plannedMinutes();
    wchar_t kicker[48];
    swprintf_s(kicker, L"THE NEXT %d MINUTES", mins);
    std::wstring intention = page_->intention();

    TextStyle sKick{ Theme::kSans, Theme::kFntCardKick, DWRITE_FONT_WEIGHT_SEMI_BOLD,
                     DWRITE_FONT_STYLE_NORMAL, DWRITE_TEXT_ALIGNMENT_CENTER };
    sKick.tracking = 5.f;
    TextStyle sBig{ Theme::kSerif, Theme::kFntCardBig, DWRITE_FONT_WEIGHT_MEDIUM,
                    DWRITE_FONT_STYLE_NORMAL, DWRITE_TEXT_ALIGNMENT_CENTER };

    float bigMaxW = std::min(720.f, W - 120.f);
    float kickH = Theme::kFntCardKick * 1.4f;
    float bigH = tl_.measureHeight(intention, sBig, bigMaxW);
    float innerH = kickH + Theme::kCardTextGap + bigH;
    float ruleBlockH = 1 + Theme::kRuleGap + innerH + Theme::kRuleGap + 1;

    // guide region height = padTop + box + label
    float guideRegionH = Theme::kGuidePadTop + Theme::kGuideBox + 24.f + Theme::kFntBreath * 1.4f;
    float totalH = ruleBlockH + guideRegionH;
    float top = (H - totalH) * 0.5f;

    float ruleX0 = (W - Theme::kRuleWidth) * 0.5f;
    float ruleX1 = ruleX0 + Theme::kRuleWidth;

    // rule 1
    b->SetColor(Theme::white(0.28f * op));
    dc->FillRectangle(D2D1::RectF(ruleX0, top, ruleX1, top + 1), b);
    // kicker
    float ky = top + 1 + Theme::kRuleGap;
    b->SetColor(Theme::white(0.55f * op));
    tl_.draw(dc, kicker, sKick, D2D1::RectF(0, ky, W, ky + kickH), b);
    // intention (big serif)
    float by = ky + kickH + Theme::kCardTextGap;
    b->SetColor(Theme::white(0.95f * op));
    tl_.draw(dc, intention, sBig, D2D1::RectF((W - bigMaxW) / 2, by, (W + bigMaxW) / 2, by + bigH + 8), b);
    // rule 2
    float r2y = top + 1 + Theme::kRuleGap + innerH + Theme::kRuleGap;
    b->SetColor(Theme::white(0.28f * op));
    dc->FillRectangle(D2D1::RectF(ruleX0, r2y, ruleX1, r2y + 1), b);

    // breathing guide, centred below the rules block
    float guideCy = r2y + 1 + Theme::kGuidePadTop + Theme::kGuideBox / 2.f;
    drawBreathGuide(dc, b, W / 2.f, guideCy, op);
}

// -------------------------------------------------------------- window proc --
LRESULT CALLBACK OverlayWindow::WndProc(HWND h, UINT msg, WPARAM w, LPARAM l) {
    if (msg == WM_NCCREATE) {
        auto* cs = reinterpret_cast<CREATESTRUCTW*>(l);
        auto* self = static_cast<OverlayWindow*>(cs->lpCreateParams);
        self->hwnd_ = h;
        SetWindowLongPtrW(h, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
    }
    auto* self = reinterpret_cast<OverlayWindow*>(GetWindowLongPtrW(h, GWLP_USERDATA));
    if (self) return self->handle(msg, w, l);
    return DefWindowProcW(h, msg, w, l);
}

LRESULT OverlayWindow::handle(UINT msg, WPARAM w, LPARAM l) {
    switch (msg) {
    case WM_PAINT: {
        PAINTSTRUCT ps; BeginPaint(hwnd_, &ps); render(); EndPaint(hwnd_, &ps);
        return 0;
    }
    case WM_ERASEBKGND: return 1;

    case WM_TIMER: {
        UINT_PTR id = (UINT_PTR)w;
        if (id == TIMER_FRAME) {
            ULONGLONG now = GetTickCount64();
            dim_.tick(now); card_.tick(now);
            // nudge two-leg: swell then settle
            if (nudge_.tick(now) == false && nudge_.to == 1.03 && nudge_.cur == 1.03)
                nudge_.go(1.0, Theme::kNudgeSettleMs, now);
            if (screen_ == Screen::TitleShow) {
                double dur = reduceMotion_ ? Theme::kTitleCardRedMs : Theme::kTitleCardMs;
                if (double(now - titleStart_) >= dur) {
                    // Stage 1: fade the card out first; the dim stays deep behind it
                    // so the desktop never shows through a half-faded card. (Swift
                    // fades the card ~1s, THEN releaseSessionDim over 2s -- sequential.)
                    screen_ = Screen::TitleRelease;
                    card_.go(0.0, Theme::kCardFadeOutMs, now);
                    Log::write(L"[overlay] -> TitleRelease (card fading)");
                }
            }
            if (screen_ == Screen::TitleRelease) {
                // Stage 2: once the card is gone, release the dim over ~2s.
                if (!card_.active && card_.cur <= 0.001 && dim_.cur > 0.001 && !dim_.active) {
                    dim_.go(0.0, Theme::kDimReleaseMs, now);
                    Log::write(L"[overlay] TitleRelease -> dim releasing");
                }
                if (!card_.active && !dim_.active && dim_.cur <= 0.001) {
                    enterHidden();
                    KillTimer(hwnd_, TIMER_FRAME);
                    return 0;
                }
            }
            render();
            if (!animating()) KillTimer(hwnd_, TIMER_FRAME);
            return 0;
        }
        if (id == TIMER_LOGIC) { page_->update(); return 0; }
        if (id == TIMER_AUTOFOCUS) {
            KillTimer(hwnd_, TIMER_AUTOFOCUS);
            if (screen_ == Screen::Idle) intentionField_.showAndFocus();
            return 0;
        }
        if (id == TIMER_BREAKSHOW) { KillTimer(hwnd_, TIMER_BREAKSHOW); enterBreak(); return 0; }
        if (id == TIMER_DUMP) {
            if (screen_ != Screen::Hidden && !dumpDir_.empty()) {
                wchar_t p[600];
                swprintf_s(p, L"%ls\\f%03d_%ls.png", dumpDir_.c_str(), dumpSeq_++, screenTag((int)screen_));
                comp_.requestDump(p);
                render();
            }
            return 0;
        }
        if (onTimer) onTimer(id);   // watchdog / countdown belong to main
        return 0;
    }

    case WM_LBUTTONDOWN: {
        float xd = GET_X_LPARAM(l) / scale_, yd = GET_Y_LPARAM(l) / scale_;
        auto inR = [&](const D2D1_RECT_F& r) {
            return xd >= r.left && xd <= r.right && yd >= r.top && yd <= r.bottom;
        };
        if (screen_ == Screen::Idle) {
            if (inR(beginRect_)) { if (page_->canBegin()) page_->begin(); return 0; }
            D2D1_RECT_F sh = D2D1::RectF(sliderRect_.left - 8, sliderRect_.top - 4,
                                         sliderRect_.right + 8, sliderRect_.bottom + 4);
            if (inR(sh)) { draggingSlider_ = true; SetCapture(hwnd_);
                           page_->setMinutes(sliderValueFromX(xd)); invalidate(); return 0; }
            if (!inR(panelRect_)) pulse();
        } else if (screen_ == Screen::Break) {
            if (inR(continueRect_)) { page_->continueFromBreak(); return 0; }
            if (!inR(panelRect_)) pulse();
        } else if (screen_ == Screen::TitleShow) {
            // Tap to skip: same choreography as a natural end -- fade the card
            // (stage 1), then the tick releases the dim (stage 2). Matches Swift,
            // where the skip tap routes through the same fadeOutCard path.
            ULONGLONG now = GetTickCount64();
            screen_ = Screen::TitleRelease;
            card_.go(0.0, Theme::kCardFadeOutMs, now);
            startFrames();
            Log::write(L"[overlay] title card skipped -> TitleRelease (card fading)");
        }
        return 0;
    }
    case WM_MOUSEMOVE:
        if (draggingSlider_) {
            float xd = GET_X_LPARAM(l) / scale_;
            page_->setMinutes(sliderValueFromX(xd)); invalidate();
        }
        return 0;
    case WM_LBUTTONUP:
        if (draggingSlider_) { draggingSlider_ = false; ReleaseCapture(); }
        return 0;

    case WM_KEYDOWN:
        if (w == VK_ESCAPE) { if (onQuitRequested) onQuitRequested(); }
        return 0;

    case WM_COMMAND:
        if (onMenuCommand) onMenuCommand(LOWORD(w));
        return 0;

    case WM_DISPLAYCHANGE:
        Log::write(L"[overlay] WM_DISPLAYCHANGE -> rebuild dimmers");
        if (onDisplayChange) onDisplayChange();
        return 0;

    case WM_WTSSESSION_CHANGE:
        Log::write(L"[overlay] WM_WTSSESSION_CHANGE wParam=%llu", (unsigned long long)w);
        if (w == WTS_SESSION_UNLOCK) {
            // Greet the unlock by re-showing the panel, unless a session is
            // running (match AppDelegate.swift's guard). During a break, bring
            // the break panel forward rather than jumping back to idle.
            if (page_->phase() == Phase::Running) {
                Log::write(L"[overlay] unlock ignored (session running)");
            } else if (page_->phase() == Phase::Resting) {
                bringToFront();
            } else {
                show();
            }
        }
        return 0;

    case WM_DPICHANGED: {
        HMONITOR m = MonitorFromWindow(hwnd_, MONITOR_DEFAULTTONEAREST);
        MONITORINFO mi{ sizeof(mi) }; GetMonitorInfoW(m, &mi);
        UINT dx = 96, dy = 96; GetDpiForMonitor(m, MDT_EFFECTIVE_DPI, &dx, &dy);
        work_ = mi.rcWork; dpi_ = dx; scale_ = dpi_ / 96.f;
        int nw = work_.right - work_.left, nh = work_.bottom - work_.top;
        SetWindowPos(hwnd_, HWND_TOPMOST, work_.left, work_.top, nw, nh, SWP_NOACTIVATE);
        comp_.resize((UINT)nw, (UINT)nh); comp_.setDpi(dpi_);
        computeLayout();
        if (screen_ == Screen::Idle)  positionField(intentionField_, fieldRect_);
        if (screen_ == Screen::Break) positionField(reflectionField_, reflectRect_);
        render();
        Log::write(L"[overlay] WM_DPICHANGED newDpi=%u", dpi_);
        if (onDisplayChange) onDisplayChange();
        return 0;
    }

    case WM_DESTROY: return 0;
    default:
        if (msg >= WM_APP && onTrayMessage) { onTrayMessage(w, l); return 0; }
        break;
    }
    return DefWindowProcW(hwnd_, msg, w, l);
}
