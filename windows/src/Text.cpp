#include "Text.h"

static bool sameStyle(const TextStyle& a, const TextStyle& b) {
    return a.family == b.family && a.size == b.size && a.weight == b.weight &&
           a.style == b.style && a.align == b.align;
}

ComPtr<IDWriteTextFormat> TextLab::format(const TextStyle& st) {
    for (auto& c : cache_)
        if (sameStyle(c.key, st)) return c.fmt;

    ComPtr<IDWriteTextFormat> fmt;
    factory_->CreateTextFormat(st.family, nullptr, st.weight, st.style,
                               DWRITE_FONT_STRETCH_NORMAL, st.size, L"en-us", &fmt);
    if (fmt) {
        fmt->SetTextAlignment(st.align);
        fmt->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_NEAR);
        fmt->SetWordWrapping(DWRITE_WORD_WRAPPING_WRAP);
        cache_.push_back({ st, fmt });
    }
    return fmt;
}

ComPtr<IDWriteTextLayout> TextLab::layout(const std::wstring& s, const TextStyle& st,
                                          float maxW, float maxH) {
    auto fmt = format(st);
    ComPtr<IDWriteTextLayout> lay;
    if (!fmt) return lay;
    factory_->CreateTextLayout(s.c_str(), static_cast<UINT32>(s.size()),
                               fmt.Get(), maxW, maxH, &lay);
    if (!lay) return lay;

    DWRITE_TEXT_RANGE all{ 0, static_cast<UINT32>(s.size()) };
    if (st.tracking != 0.f) {
        ComPtr<IDWriteTextLayout1> lay1;
        if (SUCCEEDED(lay.As(&lay1)))
            lay1->SetCharacterSpacing(0.f, st.tracking, 0.f, all);
    }
    if (st.lineSpacing != 0.f) {
        // Approximate SwiftUI lineSpacing: default line height + extra leading.
        lay->SetLineSpacing(DWRITE_LINE_SPACING_METHOD_UNIFORM,
                            st.size * 1.35f + st.lineSpacing, st.size * 1.1f);
    }
    return lay;
}

void TextLab::draw(ID2D1DeviceContext* dc, const std::wstring& s, const TextStyle& st,
                   const D2D1_RECT_F& box, ID2D1Brush* brush) {
    float w = box.right - box.left, h = box.bottom - box.top;
    auto lay = layout(s, st, w, h);
    if (!lay) return;
    dc->DrawTextLayout(D2D1::Point2F(box.left, box.top), lay.Get(), brush,
                       D2D1_DRAW_TEXT_OPTIONS_ENABLE_COLOR_FONT);
}

float TextLab::measureHeight(const std::wstring& s, const TextStyle& st, float maxW) {
    auto lay = layout(s, st, maxW, 100000.f);
    if (!lay) return 0.f;
    DWRITE_TEXT_METRICS m{};
    lay->GetMetrics(&m);
    return m.height;
}

float TextLab::measureWidth(const std::wstring& s, const TextStyle& st) {
    auto lay = layout(s, st, 100000.f, 100000.f);
    if (!lay) return 0.f;
    DWRITE_TEXT_METRICS m{};
    lay->GetMetrics(&m);
    return m.widthIncludingTrailingWhitespace;
}
