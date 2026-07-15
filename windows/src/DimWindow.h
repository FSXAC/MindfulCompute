#pragma once
// A "dumb" per-monitor dimmer: dark, topmost, click-eating, never activated.
// One is created for every monitor that is NOT hosting the smart overlay.
// Uses the same DirectComposition transparency path as the overlay.
#include <windows.h>
#include <functional>
#include "Graphics.h"

class DimWindow {
public:
    bool create(HINSTANCE hInst, GraphicsDevice* gfx, const RECT& workArea);
    void show();
    void showNA();
    void hide();
    void setAlpha(float a);     // dim level 0..~0.82; re-renders if changed
    HWND hwnd() const { return hwnd_; }

    // Set by main so a click on a dumb dimmer pulses the smart panel (matching
    // the macOS dimmer behaviour).
    std::function<void()> onClick;

    ~DimWindow();

private:
    static LRESULT CALLBACK WndProc(HWND, UINT, WPARAM, LPARAM);
    LRESULT handle(UINT, WPARAM, LPARAM);
    void    render();

    HWND              hwnd_ = nullptr;
    GraphicsDevice*   gfx_  = nullptr;
    RECT              work_{};
    CompositionTarget comp_;
    float             alpha_ = 0.f;
};
