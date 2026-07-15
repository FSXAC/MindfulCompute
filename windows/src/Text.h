#pragma once
// Small DirectWrite helper: cached text formats + one-shot layout/draw/measure
// in logical DIPs. Supports letter-spacing (tracking) and custom line spacing,
// which the SwiftUI sources use (`.tracking`, `.lineSpacing`).
#include <windows.h>
#include <dwrite_1.h>
#include <d2d1_1.h>
#include <wrl/client.h>
#include <string>
#include <vector>

using Microsoft::WRL::ComPtr;

struct TextStyle {
    const wchar_t*      family  = L"Segoe UI";
    float               size    = 15.f;
    DWRITE_FONT_WEIGHT  weight  = DWRITE_FONT_WEIGHT_NORMAL;
    DWRITE_FONT_STYLE   style   = DWRITE_FONT_STYLE_NORMAL;
    DWRITE_TEXT_ALIGNMENT align = DWRITE_TEXT_ALIGNMENT_LEADING;
    float               tracking    = 0.f;  // extra advance per glyph (DIP)
    float               lineSpacing = 0.f;  // extra leading (DIP); 0 = default
};

class TextLab {
public:
    void init(IDWriteFactory* f) { factory_ = f; }

    ComPtr<IDWriteTextLayout> layout(const std::wstring& s, const TextStyle& st,
                                     float maxW, float maxH);

    // Draws top-left-anchored within box; box width controls alignment.
    void  draw(ID2D1DeviceContext* dc, const std::wstring& s, const TextStyle& st,
               const D2D1_RECT_F& box, ID2D1Brush* brush);

    // Height the text occupies at width maxW (for top-down panel layout).
    float measureHeight(const std::wstring& s, const TextStyle& st, float maxW);
    float measureWidth(const std::wstring& s, const TextStyle& st);

private:
    ComPtr<IDWriteTextFormat> format(const TextStyle& st);

    IDWriteFactory* factory_ = nullptr;
    struct Cached { TextStyle key; ComPtr<IDWriteTextFormat> fmt; };
    std::vector<Cached> cache_;
};
