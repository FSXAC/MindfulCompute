#pragma once
// Shared graphics device + per-window composition target.
//
// This is the reusable core Phase 1 builds on: one GraphicsDevice (D3D11 + D2D1
// + DirectComposition device, created once) is shared by every window, and each
// transparent window owns a CompositionTarget (a premultiplied-alpha DXGI swap
// chain presented through a DComp visual, with a D2D device context that draws
// into it). This is the modern per-pixel-transparency path -- NOT
// UpdateLayeredWindow.
#include <windows.h>
#include <d3d11.h>
#include <dxgi1_2.h>
#include <d2d1_1.h>
#include <dwrite.h>
#include <dcomp.h>
#include <wrl/client.h>
#include <string>

using Microsoft::WRL::ComPtr;

class GraphicsDevice {
public:
    bool init();

    ID2D1Device*         d2dDevice()   const { return d2dDevice_.Get(); }
    IDXGIFactory2*       dxgiFactory() const { return dxgiFactory_.Get(); }
    IDCompositionDevice* dcomp()       const { return dcompDevice_.Get(); }
    IUnknown*            d3dUnknown()  const { return d3dDevice_.Get(); }
    IDWriteFactory*      dwrite()      const { return dwriteFactory_.Get(); }

private:
    ComPtr<ID3D11Device>        d3dDevice_;
    ComPtr<IDXGIDevice>         dxgiDevice_;
    ComPtr<IDXGIFactory2>       dxgiFactory_;
    ComPtr<ID2D1Factory1>       d2dFactory_;
    ComPtr<ID2D1Device>         d2dDevice_;
    ComPtr<IDCompositionDevice> dcompDevice_;
    ComPtr<IDWriteFactory>      dwriteFactory_;
};

class CompositionTarget {
public:
    // Attaches a composition swap chain to hwnd at the given PHYSICAL pixel size.
    // dpi drives the D2D context so callers draw in logical units (DIPs) and D2D
    // scales to pixels -- so the same geometry renders correctly at any scale.
    bool init(GraphicsDevice* gfx, HWND hwnd, UINT w, UINT h, UINT dpi = 96);

    // begin() returns a device context already targeting the back buffer with
    // BeginDraw called; end() finishes, presents, and (first time) commits.
    ID2D1DeviceContext* begin();
    void                end();

    bool resize(UINT w, UINT h);
    void setDpi(UINT dpi);

    // Debug/QA aid (no interactive-desktop screen capture is possible in a
    // headless/automation session). When a path is requested, the NEXT end()
    // captures the frame it just drew -- the swap chain's back buffer, composited
    // over an opaque neutral backdrop so the dim reads -- and writes it as a PNG.
    // Faithful to exactly what the overlay renders. Gated by MINDFUL_DUMP_DIR.
    void requestDump(const std::wstring& path) { dumpPath_ = path; }

    UINT  width()  const { return w_; }              // physical pixels
    UINT  height() const { return h_; }              // physical pixels
    float dipWidth()  const { return w_ * 96.f / dpi_; }
    float dipHeight() const { return h_ * 96.f / dpi_; }

private:
    bool createBitmap();
    void dumpBackBuffer(const std::wstring& path);

    std::wstring                 dumpPath_;   // pending one-shot PNG dump (see requestDump)
    GraphicsDevice*              gfx_ = nullptr;
    ComPtr<IDXGISwapChain1>      swap_;
    ComPtr<ID2D1DeviceContext>   dc_;
    ComPtr<IDCompositionTarget>  target_;
    ComPtr<IDCompositionVisual>  visual_;
    UINT w_ = 0, h_ = 0;
    UINT dpi_ = 96;
    bool committed_ = false;
};
