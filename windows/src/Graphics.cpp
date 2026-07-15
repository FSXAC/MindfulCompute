#include "Graphics.h"
#include "Log.h"
#include <wincodec.h>
#include <vector>
#include <algorithm>

#define HRLOG(expr, what)                                                     \
    do {                                                                      \
        HRESULT _hr = (expr);                                                 \
        if (FAILED(_hr)) {                                                    \
            Log::write(L"[gfx] FAIL %ls hr=0x%08X", L##what, (unsigned)_hr);  \
            return false;                                                     \
        }                                                                     \
    } while (0)

bool GraphicsDevice::init() {
    UINT flags = D3D11_CREATE_DEVICE_BGRA_SUPPORT;
#ifdef _DEBUG
    // flags |= D3D11_CREATE_DEVICE_DEBUG; // enable if the debug layer is present
#endif
    D3D_FEATURE_LEVEL levels[] = {
        D3D_FEATURE_LEVEL_11_1, D3D_FEATURE_LEVEL_11_0, D3D_FEATURE_LEVEL_10_1,
        D3D_FEATURE_LEVEL_10_0, D3D_FEATURE_LEVEL_9_3,  D3D_FEATURE_LEVEL_9_1,
    };
    HRLOG(D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, flags,
                            levels, ARRAYSIZE(levels), D3D11_SDK_VERSION,
                            &d3dDevice_, nullptr, nullptr),
          "D3D11CreateDevice(hardware)");

    HRLOG(d3dDevice_.As(&dxgiDevice_), "QueryInterface IDXGIDevice");
    HRLOG(CreateDXGIFactory2(0, IID_PPV_ARGS(&dxgiFactory_)), "CreateDXGIFactory2");

    D2D1_FACTORY_OPTIONS opts{};
    HRLOG(D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED,
                            __uuidof(ID2D1Factory1), &opts, &d2dFactory_),
          "D2D1CreateFactory");
    HRLOG(d2dFactory_->CreateDevice(dxgiDevice_.Get(), &d2dDevice_),
          "ID2D1Factory1::CreateDevice");

    HRLOG(DCompositionCreateDevice(dxgiDevice_.Get(),
                                   IID_PPV_ARGS(&dcompDevice_)),
          "DCompositionCreateDevice");

    HRLOG(DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory),
                              reinterpret_cast<IUnknown**>(dwriteFactory_.GetAddressOf())),
          "DWriteCreateFactory");

    Log::write(L"[gfx] device ready (D3D11 + D2D1 + DirectWrite + DirectComposition)");
    return true;
}

bool CompositionTarget::init(GraphicsDevice* gfx, HWND hwnd, UINT w, UINT h, UINT dpi) {
    gfx_ = gfx;
    w_ = (w == 0) ? 1 : w;
    h_ = (h == 0) ? 1 : h;
    dpi_ = (dpi == 0) ? 96 : dpi;

    HRLOG(gfx_->d2dDevice()->CreateDeviceContext(D2D1_DEVICE_CONTEXT_OPTIONS_NONE, &dc_),
          "CreateDeviceContext");
    // Draw in logical DIPs; D2D scales to the swap chain's physical pixels.
    dc_->SetDpi(static_cast<float>(dpi_), static_cast<float>(dpi_));

    DXGI_SWAP_CHAIN_DESC1 sc{};
    sc.Width       = w_;
    sc.Height      = h_;
    sc.Format      = DXGI_FORMAT_B8G8R8A8_UNORM;
    sc.SampleDesc.Count = 1;
    sc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    sc.BufferCount = 2;
    sc.SwapEffect  = DXGI_SWAP_EFFECT_FLIP_SEQUENTIAL;
    sc.AlphaMode   = DXGI_ALPHA_MODE_PREMULTIPLIED; // premultiplied per the brief
    sc.Scaling     = DXGI_SCALING_STRETCH;

    HRLOG(gfx_->dxgiFactory()->CreateSwapChainForComposition(
              gfx_->d3dUnknown(), &sc, nullptr, &swap_),
          "CreateSwapChainForComposition");

    if (!createBitmap()) return false;

    HRLOG(gfx_->dcomp()->CreateTargetForHwnd(hwnd, TRUE, &target_),
          "CreateTargetForHwnd");
    HRLOG(gfx_->dcomp()->CreateVisual(&visual_), "CreateVisual");
    HRLOG(visual_->SetContent(swap_.Get()), "Visual::SetContent");
    HRLOG(target_->SetRoot(visual_.Get()), "Target::SetRoot");
    HRLOG(gfx_->dcomp()->Commit(), "DComp::Commit");
    committed_ = true;
    return true;
}

bool CompositionTarget::createBitmap() {
    ComPtr<IDXGISurface> surface;
    HRLOG(swap_->GetBuffer(0, IID_PPV_ARGS(&surface)), "SwapChain::GetBuffer");

    auto props = D2D1::BitmapProperties1(
        D2D1_BITMAP_OPTIONS_TARGET | D2D1_BITMAP_OPTIONS_CANNOT_DRAW,
        D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED),
        static_cast<float>(dpi_), static_cast<float>(dpi_));
    ComPtr<ID2D1Bitmap1> bmp;
    HRLOG(dc_->CreateBitmapFromDxgiSurface(surface.Get(), props, &bmp),
          "CreateBitmapFromDxgiSurface");
    dc_->SetTarget(bmp.Get());
    return true;
}

ID2D1DeviceContext* CompositionTarget::begin() {
    dc_->BeginDraw();
    return dc_.Get();
}

void CompositionTarget::end() {
    HRESULT hr = dc_->EndDraw();
    if (FAILED(hr)) {
        Log::write(L"[gfx] EndDraw hr=0x%08X", (unsigned)hr);
    }
    // Capture BEFORE Present: in the flip model, buffer 0 is still the frame we
    // just drew until Present rotates it.
    if (!dumpPath_.empty()) {
        dumpBackBuffer(dumpPath_);
        dumpPath_.clear();
    }
    swap_->Present(1, 0);
    if (!committed_) {
        gfx_->dcomp()->Commit();
        committed_ = true;
    }
}

// Read back buffer 0, composite the premultiplied-alpha pixels over an opaque
// neutral backdrop (so the dim/panel read as they do on a desktop), PNG-encode
// via WIC. QA-only; never on the hot path.
void CompositionTarget::dumpBackBuffer(const std::wstring& path) {
    ComPtr<ID3D11Texture2D> back;
    if (FAILED(swap_->GetBuffer(0, IID_PPV_ARGS(&back)))) return;
    D3D11_TEXTURE2D_DESC d{}; back->GetDesc(&d);

    ComPtr<ID3D11Device> dev;
    if (FAILED(gfx_->d3dUnknown()->QueryInterface(IID_PPV_ARGS(&dev)))) return;
    D3D11_TEXTURE2D_DESC sd = d;
    sd.Usage = D3D11_USAGE_STAGING;
    sd.BindFlags = 0;
    sd.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    sd.MiscFlags = 0;
    ComPtr<ID3D11Texture2D> staging;
    if (FAILED(dev->CreateTexture2D(&sd, nullptr, &staging))) return;
    ComPtr<ID3D11DeviceContext> ctx; dev->GetImmediateContext(&ctx);
    ctx->CopyResource(staging.Get(), back.Get());

    D3D11_MAPPED_SUBRESOURCE m{};
    if (FAILED(ctx->Map(staging.Get(), 0, D3D11_MAP_READ, 0, &m))) return;

    const UINT W = d.Width, H = d.Height;
    std::vector<BYTE> out(size_t(W) * H * 4);
    const BYTE bg[3] = { 128, 122, 116 }; // B,G,R -- a muted neutral "desktop"
    for (UINT y = 0; y < H; ++y) {
        const BYTE* src = static_cast<const BYTE*>(m.pData) + size_t(y) * m.RowPitch;
        BYTE* dst = out.data() + size_t(y) * W * 4;
        for (UINT x = 0; x < W; ++x) {
            BYTE b = src[x*4+0], g = src[x*4+1], r = src[x*4+2], a = src[x*4+3];
            float inv = (255 - a) / 255.f;   // src is premultiplied: out = src + bg*(1-a)
            dst[x*4+0] = (BYTE)std::min(255, int(b + bg[0]*inv + 0.5f));
            dst[x*4+1] = (BYTE)std::min(255, int(g + bg[1]*inv + 0.5f));
            dst[x*4+2] = (BYTE)std::min(255, int(r + bg[2]*inv + 0.5f));
            dst[x*4+3] = 255;
        }
    }
    ctx->Unmap(staging.Get(), 0);

    ComPtr<IWICImagingFactory> wic;
    if (FAILED(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER,
                                IID_PPV_ARGS(&wic)))) return;
    ComPtr<IWICStream> stream;
    if (FAILED(wic->CreateStream(&stream)) ||
        FAILED(stream->InitializeFromFilename(path.c_str(), GENERIC_WRITE))) return;
    ComPtr<IWICBitmapEncoder> enc;
    if (FAILED(wic->CreateEncoder(GUID_ContainerFormatPng, nullptr, &enc)) ||
        FAILED(enc->Initialize(stream.Get(), WICBitmapEncoderNoCache))) return;
    ComPtr<IWICBitmapFrameEncode> frame; ComPtr<IPropertyBag2> props;
    if (FAILED(enc->CreateNewFrame(&frame, &props)) || FAILED(frame->Initialize(props.Get()))) return;
    frame->SetSize(W, H);
    WICPixelFormatGUID fmt = GUID_WICPixelFormat32bppBGRA;
    frame->SetPixelFormat(&fmt);
    frame->WritePixels(H, W * 4, (UINT)out.size(), out.data());
    frame->Commit();
    enc->Commit();
    Log::write(L"[gfx] dumped frame -> %ls (%ux%u)", path.c_str(), W, H);
}

bool CompositionTarget::resize(UINT w, UINT h) {
    if (w == 0) w = 1;
    if (h == 0) h = 1;
    if (w == w_ && h == h_) return true;
    dc_->SetTarget(nullptr);
    HRLOG(swap_->ResizeBuffers(0, w, h, DXGI_FORMAT_UNKNOWN, 0),
          "SwapChain::ResizeBuffers");
    w_ = w; h_ = h;
    return createBitmap();
}

void CompositionTarget::setDpi(UINT dpi) {
    if (dpi == 0) dpi = 96;
    if (dpi == dpi_) return;
    dpi_ = dpi;
    dc_->SetTarget(nullptr);
    dc_->SetDpi(static_cast<float>(dpi_), static_cast<float>(dpi_));
    createBitmap();
}
