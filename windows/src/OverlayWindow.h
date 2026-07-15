#pragma once
// The one "smart" window: a DirectComposition transparent, topmost tool window
// on the cursor's monitor. It draws all three screens (intention panel, title
// card + breathing guide, break panel) layered by draw order over an animated
// dim, hosts the two EDIT fields, and runs the transition choreography.
//
// It reads session state through ISessionState (never scattered globals) and
// drives transitions off PageController::onPhaseChanged, so Phase 2 can swap in
// the real wall-clock SessionManager behind the same interface. Orchestration
// (tray, watchdog, dim windows, quit) stays in main via the hooks below.
#include <windows.h>
#include <functional>
#include "Graphics.h"
#include "Text.h"
#include "TextField.h"
#include "Session.h"

// A minimal ease-in-out tween in milliseconds.
struct Tween {
    double from = 0, to = 0, cur = 0;
    ULONGLONG start = 0;
    double dur = 0;
    bool active = false;
    void  set(double v) { from = to = cur = v; active = false; }
    void  go(double target, double durMs, ULONGLONG now);
    bool  tick(ULONGLONG now);   // advance; returns true while still animating
};

class OverlayWindow {
public:
    bool create(HINSTANCE hInst, GraphicsDevice* gfx, PageController* page,
                const RECT& workArea, UINT dpi);
    void show();          // idle entrance: panel appears, dim gathers ~2s
    void pulse();         // nudge the panel (a click on the dim)
    void bringToFront();  // tray "bring panel/break to front"
    HWND hwnd() const { return hwnd_; }

    // Hooks wired by main().
    std::function<void()>               onQuitRequested;
    std::function<void()>               onDisplayChange;
    std::function<void(WPARAM,LPARAM)>  onTrayMessage;
    std::function<void(int)>            onMenuCommand;
    std::function<void(UINT_PTR)>       onTimer;        // watchdog/countdown (main's IDs)
    std::function<void(float,bool)>     onDimChanged;   // -> DimManager::apply(level, visible)
    std::function<void(Phase)>          onPhaseObserved;// -> main updates the tray

private:
    enum class Screen { Idle, TitleEnter, TitleShow, TitleRelease, Hidden, BreakGather, Break };

    static LRESULT CALLBACK WndProc(HWND, UINT, WPARAM, LPARAM);
    LRESULT handle(UINT, WPARAM, LPARAM);

    void onPhase(Phase p);
    void enterIdle(bool firstShow);
    void enterTitle();
    void enterHidden();
    void enterBreakGather();
    void enterBreak();

    void computeLayout();               // fills panel/element rects (logical DIPs)
    void render();
    void drawStart(ID2D1DeviceContext*, ID2D1SolidColorBrush*);
    void drawBreak(ID2D1DeviceContext*, ID2D1SolidColorBrush*);
    void drawTitleCard(ID2D1DeviceContext*, ID2D1SolidColorBrush*);
    void drawBreathGuide(ID2D1DeviceContext*, ID2D1SolidColorBrush*, float cx, float cy, float opacity);
    void drawCardChrome(ID2D1DeviceContext*, ID2D1SolidColorBrush*, D2D1_COLOR_F accent);
    // Field fill + (when focused) an accent focus ring drawn just outside the
    // opaque EDIT host. Shared by the start and break screens so both fields are
    // styled identically.
    void drawFieldChrome(ID2D1DeviceContext*, ID2D1SolidColorBrush*,
                         const D2D1_RECT_F& rect, bool focused, D2D1_COLOR_F accent);

    void  startFrames();
    void  stopFramesIfIdle();
    void  invalidate();
    bool  animating() const;
    RECT  toScreen(const D2D1_RECT_F& dip) const;   // logical DIP -> screen px
    void  positionField(TextField& f, const D2D1_RECT_F& dip);
    int   sliderValueFromX(float xDip) const;
    void  setBadgeFromPhase();

    // Card dragging: clamp dragOffset_ (DIPs) so the panel of size panelW x
    // panelH, whose un-dragged top-left is (px,py), keeps a generous margin
    // on-screen. Called from computeLayout where panelH is known.
    void  clampDragOffset(float px, float py, float panelW, float panelH);
    // Reposition the field host that belongs to the current screen (so it
    // tracks the card while dragging). No-op for screens without a field.
    void  repositionActiveField();

    HWND              hwnd_ = nullptr;
    GraphicsDevice*   gfx_  = nullptr;
    PageController*   page_ = nullptr;
    RECT              work_{};
    UINT              dpi_  = 96;
    float             scale_ = 1.f;
    bool              reduceMotion_ = false;
    CompositionTarget comp_;
    TextLab           tl_;

    TextField         intentionField_;
    TextField         reflectionField_;
    ComPtr<ID2D1SolidColorBrush> brush_;

    Screen  screen_ = Screen::Idle;
    Tween   dim_;         // dim level
    Tween   card_;        // title card opacity
    Tween   nudge_;       // panel pulse scale (1..1.03)
    ULONGLONG titleStart_ = 0;
    ULONGLONG breakStart_ = 0;
    bool    draggingSlider_ = false;

    // Card dragging (start & break panels). The card is not a window; dragging
    // just offsets where it's drawn (dragOffset_, in DIPs) and re-lays out.
    // Reset to 0 on each panel entrance so the card re-centres on next show.
    float   dragOffsetX_ = 0.f;
    float   dragOffsetY_ = 0.f;
    bool    draggingCard_ = false;   // between LBUTTONDOWN in card and release
    bool    dragMoved_    = false;   // passed the ~4px threshold this drag
    int     dragStartMouseX_ = 0;    // physical px at button-down
    int     dragStartMouseY_ = 0;
    float   dragStartOffX_ = 0.f;    // dragOffset_ captured at button-down (DIPs)
    float   dragStartOffY_ = 0.f;

    // QA frame dumps (MINDFUL_DUMP_DIR): periodic PNG of exactly what's drawn,
    // since live screen capture isn't possible in a headless automation session.
    std::wstring dumpDir_;
    int          dumpSeq_ = 0;

    // Layout (logical DIPs, relative to window client top-left).
    D2D1_RECT_F panelRect_{};
    D2D1_RECT_F fieldRect_{};
    D2D1_RECT_F sliderRect_{};
    D2D1_RECT_F beginRect_{};
    D2D1_RECT_F reflectRect_{};
    D2D1_RECT_F continueRect_{};
};
