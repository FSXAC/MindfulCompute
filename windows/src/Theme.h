#pragma once
// One place for every colour / spacing / type-size / timing constant, per the
// brief. Values are LOGICAL units (DIPs) and point sizes, matching the macOS
// SwiftUI sources 1:1 -- the overlay's Direct2D context is set to the monitor's
// effective DPI, so the same numbers render correctly at any scale.
//
// Colours are drawn into a B8G8R8A8_UNORM (non-sRGB) swap chain, so component
// values are interpreted as straight sRGB-encoded values. The two accents come
// from Views/PanelRoot.swift as Display-P3 and are converted to sRGB below.
#include <d2d1.h>

namespace Theme {

// ------------------------------------------------------------------ fonts ----
inline const wchar_t* kSerif = L"Georgia";        // body headings / intention
inline const wchar_t* kSans  = L"Segoe UI";       // labels / eyebrows / buttons

// -------------------------------------------------------------- panel geo ----
constexpr float kPanelWidth   = 400.f;   // .frame(width: 400)
constexpr float kPanelRadius  = 26.f;    // RoundedRectangle(cornerRadius: 26)
constexpr float kPanelPad     = 28.f;    // .padding(28)
constexpr float kFieldRadius  = 11.f;    // intention/reflection field corners
constexpr float kFieldRingWidth = 1.f;   // focus ring stroke (StartView strokeBorder lineWidth 1)
constexpr float kFieldHeight  = 44.f;    // font 15 + vertical padding 11*2
constexpr float kButtonHeight = 38.f;    // .controlSize(.large)
constexpr float kButtonRadius = 8.f;

// ------------------------------------------------------------ accents (P3) ----
// sage  = Display-P3(0.45, 0.54, 0.42) -> sRGB(0.4265, 0.5434, 0.4080)
// ember = Display-P3(0.72, 0.55, 0.36) -> sRGB(0.7518, 0.5413, 0.3263)
// (linearise P3 via sRGB TRC -> P3->sRGB linear matrix -> re-encode sRGB TRC)
inline D2D1_COLOR_F sage(float a = 1.f)  { return D2D1::ColorF(0.4265f, 0.5434f, 0.4080f, a); }
inline D2D1_COLOR_F ember(float a = 1.f) { return D2D1::ColorF(0.7518f, 0.5413f, 0.3263f, a); }

// -------------------------------------------------------------- the dim ------
// Black over the desktop; alpha is the animated dim "level" (matches macOS
// DimSheetView: rest 0.60, deep 0.82 behind the title card).
inline D2D1_COLOR_F dimColor(float level) { return D2D1::ColorF(0.f, 0.f, 0.f, level); }
constexpr float kDimRest = 0.60f;
constexpr float kDimDeep = 0.82f;

// -------------------------------------------------------------- the panel ----
// Dark translucent "glass" over the dimmed desktop -> light text.
inline D2D1_COLOR_F panelFill()   { return D2D1::ColorF(0.13f, 0.14f, 0.17f, 0.92f); }
inline D2D1_COLOR_F panelBorder() { return D2D1::ColorF(1.f, 1.f, 1.f, 0.12f); }
inline D2D1_COLOR_F fieldFill()   { return D2D1::ColorF(1.f, 1.f, 1.f, 0.06f); } // .quinary-ish
inline D2D1_COLOR_F fieldBorder() { return D2D1::ColorF(1.f, 1.f, 1.f, 0.10f); }

// Text tints on the dark glass (SwiftUI semantic opacities on a dark ground).
inline D2D1_COLOR_F txtPrimary(float a = 1.f)    { return D2D1::ColorF(1.f, 1.f, 1.f, 0.92f * a); }
inline D2D1_COLOR_F txtSecondary(float a = 1.f)  { return D2D1::ColorF(1.f, 1.f, 1.f, 0.55f * a); }
inline D2D1_COLOR_F txtTertiary(float a = 1.f)   { return D2D1::ColorF(1.f, 1.f, 1.f, 0.38f * a); }
inline D2D1_COLOR_F txtQuaternary(float a = 1.f) { return D2D1::ColorF(1.f, 1.f, 1.f, 0.25f * a); }
inline D2D1_COLOR_F white(float a)               { return D2D1::ColorF(1.f, 1.f, 1.f, a); }

// ------------------------------------------------------------ type sizes -----
// All from the SwiftUI sources (footnote~12, callout~13, caption~11, caption2~11).
constexpr float kFntGreeting = 12.f;   // StartView greeting (footnote)
constexpr float kFntTitle    = 23.f;   // "What are you here to do?" (serif medium)
constexpr float kFntField    = 15.f;   // field text
constexpr float kFntCallout  = 13.f;   // "For N minutes"
constexpr float kFntCaption2 = 11.f;   // 5 min / 1 hour / version
constexpr float kFntEyebrow  = 11.f;   // break eyebrow (tracking 4)
constexpr float kFntQuote    = 15.f;   // serif italic
constexpr float kFntCaption  = 11.f;   // author
constexpr float kFntCardKick = 13.f;   // "THE NEXT N MINUTES" (tracking 5)
constexpr float kFntCardBig  = 42.f;   // intention on the card (serif medium)
constexpr float kFntBreath   = 15.f;   // breathing label (tracking 2.5)
constexpr float kFntButton   = 15.f;

// --------------------------------------------------------- title card geo ----
constexpr float kRuleWidth   = 340.f;  // TitleCardView rule
constexpr float kRuleGap     = 36.f;   // spacing between rules and text block
constexpr float kCardTextGap = 18.f;   // kicker <-> intention
constexpr float kGuidePadTop = 72.f;   // guide below the rules block
constexpr float kGuideBox    = 130.f;  // ZStack frame
constexpr float kHaloBase    = 72.f;   // halo / ring base diameter
constexpr float kDotBase     = 10.f;   // centre dot base diameter

// ------------------------------------------------------------- timings -------
// milliseconds unless noted.
constexpr double kDimGatherMs   = 2000.0;  // launch/unlock: dim gathers ~2s
                                           //   (PanelController.showPanelFirst: context.duration = 2.0)
constexpr double kDimDeepenMs   = 1000.0;  // Begin: deepen behind the card
                                           //   (PanelController.beginSessionTransition: setDim(deep, duration: 1.0))
constexpr double kDimReleaseMs  = 2000.0;  // title card done: dim releases
                                           //   (PanelController.releaseSessionDim: context.duration = 2.0)
constexpr double kDimBreakMs    = 1200.0;  // session end: dim gathers before break
                                           //   (PanelController.showDimFirst: context.duration = 1.2)
constexpr double kBreakDelayMs  = 1300.0;  // gather, THEN the break panel appears
                                           //   (PanelController.showDimFirst: Task.sleep(1.3))
constexpr double kCardFadeInMs  = 1200.0;  // title card fade IN
                                           //   (OverlayView: .easeInOut(duration: 1.2) when cardOpacity > 0)
constexpr double kCardFadeOutMs = 1000.0;  // title card fade OUT (natural end AND skip)
                                           //   (OverlayView: .easeInOut(duration: 1.0) when cardOpacity == 0,
                                           //    driven by OverlayController.fadeOutCard)
constexpr double kTitleCardMs   = 12000.0; // card lifetime (box-breath is 12s)
                                           //   (OverlayController.runTitleSequence: cardSeconds = 12)
constexpr double kTitleCardRedMs= 5000.0;  // reduced-motion card lifetime (cardSeconds = 5)
constexpr double kNudgeSwellMs  = 220.0;   // panel pulse swell to 1.03
                                           //   (PanelRoot: .spring(duration: 0.22) -> nudged = true)
constexpr double kNudgeSettleMs = 450.0;   // panel pulse settle back to 1.0
                                           //   (PanelRoot: .spring(duration: 0.45) -> nudged = false, ~0.24s later)
constexpr double kAutofocusMs   = 300.0;   // field focus after appear
                                           //   (StartView.onAppear: asyncAfter(0.3))
constexpr double kBreathCycleMs = 12000.0; // 4s in / 4s hold / 4s out
                                           //   (TitleCardView.BreathGuide: 4s ease + 4s hold + 4s ease)

// ------------------------------------------------ GDI colours (COLORREF) -----
// The EDIT control + tray icon are GDI, so they need opaque COLORREF values.
inline COLORREF fieldTextRGB() { return RGB(236, 238, 242); }   // light-on-dark
inline COLORREF fieldBackRGB() { return RGB(46, 48, 56); }      // recessed field
inline COLORREF trayBadgeRGB() { return RGB(115, 138, 107); }   // ~sage as GDI
inline COLORREF trayEmberRGB() { return RGB(178, 130, 84); }    // ~ember as GDI

} // namespace Theme
