#include "DimManager.h"
#include "Log.h"

namespace {
struct EnumCtx {
    HINSTANCE       hInst;
    GraphicsDevice* gfx;
    HMONITOR        overlayMon;
    std::vector<std::unique_ptr<DimWindow>>* out;
    int             total = 0;
};

BOOL CALLBACK enumProc(HMONITOR mon, HDC, LPRECT, LPARAM lp) {
    auto* ctx = reinterpret_cast<EnumCtx*>(lp);
    ctx->total++;

    MONITORINFOEXW mi{};
    mi.cbSize = sizeof(mi);
    GetMonitorInfoW(mon, &mi);
    const RECT& r = mi.rcMonitor;
    const RECT& wk = mi.rcWork;
    const bool isOverlay = (mon == ctx->overlayMon);
    const bool isPrimary = (mi.dwFlags & MONITORINFOF_PRIMARY) != 0;

    Log::write(L"[mon] %ls  bounds=(%ld,%ld,%ld,%ld) work=(%ld,%ld,%ld,%ld) "
               L"primary=%d overlay=%d",
               mi.szDevice, r.left, r.top, r.right, r.bottom,
               wk.left, wk.top, wk.right, wk.bottom,
               isPrimary ? 1 : 0, isOverlay ? 1 : 0);

    if (!isOverlay) {
        auto dim = std::make_unique<DimWindow>();
        if (dim->create(ctx->hInst, ctx->gfx, wk)) {
            ctx->out->push_back(std::move(dim));  // shown/driven via apply()
        }
    }
    return TRUE;
}
} // namespace

int DimManager::build(HINSTANCE hInst, GraphicsDevice* gfx, HMONITOR overlayMonitor) {
    EnumCtx ctx{ hInst, gfx, overlayMonitor, &dims_ };
    EnumDisplayMonitors(nullptr, nullptr, &enumProc, reinterpret_cast<LPARAM>(&ctx));
    for (auto& d : dims_) d->onClick = onClick;
    Log::write(L"[dim] enumeration complete: %d monitor(s), %d dimmer(s) created",
               ctx.total, static_cast<int>(dims_.size()));
    return static_cast<int>(dims_.size());
}

void DimManager::apply(float level, bool visible) {
    if (!visible) {
        if (visible_) { for (auto& d : dims_) d->hide(); visible_ = false; }
        return;
    }
    if (!visible_) { for (auto& d : dims_) d->showNA(); visible_ = true; }
    for (auto& d : dims_) d->setAlpha(level);
}

void DimManager::teardown() {
    Log::write(L"[dim] teardown %d dimmer(s)", static_cast<int>(dims_.size()));
    dims_.clear(); // DimWindow dtor destroys each HWND
}

void DimManager::rebuild(HINSTANCE hInst, GraphicsDevice* gfx, HMONITOR overlayMonitor) {
    Log::write(L"[dim] rebuild triggered (display/DPI change)");
    teardown();
    build(hInst, gfx, overlayMonitor);
}
