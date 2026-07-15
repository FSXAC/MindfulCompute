#pragma once
// Owns the N dumb dimmers (one per non-overlay monitor). Enumerates monitors via
// EnumDisplayMonitors and, crucially, tears down + rebuilds on WM_DISPLAYCHANGE /
// WM_DPICHANGED so an orphaned dimmer never lingers over a vanished monitor.
#include <windows.h>
#include <vector>
#include <memory>
#include <functional>
#include "DimWindow.h"

class DimManager {
public:
    // Builds dimmers for every monitor except the one hosting the overlay.
    // Returns the number of dimmers created. Logs the full enumeration.
    int build(HINSTANCE hInst, GraphicsDevice* gfx, HMONITOR overlayMonitor);
    void rebuild(HINSTANCE hInst, GraphicsDevice* gfx, HMONITOR overlayMonitor);
    void teardown();
    int  count() const { return static_cast<int>(dims_.size()); }

    // Drive every dumb dimmer's level/visibility in lock-step with the smart
    // window's dim, so all monitors fade together (brief: "other monitors'
    // dumb dimmers follow the same fades").
    void apply(float level, bool visible);

    // Routed to every dimmer so a click on any monitor pulses the panel.
    std::function<void()> onClick;

private:
    std::vector<std::unique_ptr<DimWindow>> dims_;
    bool  visible_ = false;
};
