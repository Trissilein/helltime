#include "main_window.h"
#include "design_tokens.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <string_view>
#include <utility>
#include <vector>
#include <windowsx.h>

namespace helltime::ui {
namespace {

template <typename T>
void Release(T*& value) noexcept {
    if (value) {
        value->Release();
        value = nullptr;
    }
}

int IndexOf(Category category) noexcept {
    return static_cast<int>(category);
}

Category CategoryAt(int index) noexcept {
    return static_cast<Category>(std::clamp(index, 0, 2));
}

D2D1_COLOR_F CategoryColor(Category category, float alpha = 1.0f) noexcept {
    D2D1_COLOR_F color = design::Helltide;
    if (category == Category::Legion) color = design::Legion;
    if (category == Category::WorldBoss) color = design::WorldBoss;
    color.a = alpha;
    return color;
}

float Clamp01(float value) noexcept {
    return std::clamp(value, 0.0f, 1.0f);
}

float ClampScale(float value) noexcept {
    return std::clamp(value, 0.6f, 2.0f);
}

struct Rect {
    D2D1_RECT_F value{};
};

bool Contains(const D2D1_RECT_F& rect, float x, float y) noexcept {
    return x >= rect.left && x <= rect.right && y >= rect.top && y <= rect.bottom;
}

} // namespace

struct MainWindowUi::Impl {
    enum class HitKind : unsigned char {
        Settings,
        CloseSettings,
        CardHeader,
        CategoryEnabled,
        TimerCount,
        TimerMinutes,
        TimerTts,
        TimerBeep,
        TimerPitch,
        ModalPanel,
        OverlayEnabled,
        OverlayMode,
        OverlayCategory,
        OverlayScaleX,
        OverlayScaleY,
        OverlayOpacity,
        PreviewOverlay,
        BeginOverlayMove,
        ResetOverlayPosition,
        ToggleOverlayDebug,
        RefreshOverlayDiagnostics,
        ClearOverlayDiagnostics,
        Volume,
        SoundEnabled,
        AutoRefreshEnabled,
        SystemToastsEnabled,
        PanicReset,
    };

    struct Hit {
        D2D1_RECT_F rect{};
        HitKind kind{HitKind::Settings};
        Category category{Category::Helltide};
        int timerIndex{-1};
        int value{0};
    };

    HWND window{nullptr};
    UiState state{};
    UiCallbacks callbacks{};
    bool settingsOpen{false};
    bool overlayDebugOpen{false};
    std::vector<Hit> hits{};
    HitKind dragging{HitKind::Settings};
    Category draggingCategory{Category::Helltide};
    int draggingTimer{-1};

    ID2D1Factory* d2dFactory{nullptr};
    IDWriteFactory* writeFactory{nullptr};
    ID2D1HwndRenderTarget* renderTarget{nullptr};
    ID2D1SolidColorBrush* brush{nullptr};
    ID2D1SolidColorBrush* textBrush{nullptr};
    ID2D1SolidColorBrush* secondaryBrush{nullptr};
    ID2D1SolidColorBrush* mutedBrush{nullptr};
    ID2D1SolidColorBrush* borderBrush{nullptr};
    ID2D1SolidColorBrush* goldBrush{nullptr};
    ID2D1SolidColorBrush* redBrush{nullptr};

    IDWriteTextFormat* titleFormat{nullptr};
    IDWriteTextFormat* headingFormat{nullptr};
    IDWriteTextFormat* bodyFormat{nullptr};
    IDWriteTextFormat* smallFormat{nullptr};
    IDWriteTextFormat* countdownFormat{nullptr};
    IDWriteTextFormat* buttonFormat{nullptr};

    ~Impl() {
        DiscardDeviceResources();
        Release(titleFormat);
        Release(headingFormat);
        Release(bodyFormat);
        Release(smallFormat);
        Release(countdownFormat);
        Release(buttonFormat);
        Release(writeFactory);
        Release(d2dFactory);
    }

    void Invalidate() const {
        if (window) InvalidateRect(window, nullptr, FALSE);
    }

    void Emit(UiAction action) {
        if (callbacks.onAction) callbacks.onAction(action);
    }

    HRESULT CreateTextFormat(float size, DWRITE_FONT_WEIGHT weight, IDWriteTextFormat** result,
                             const wchar_t* family = design::FontUi) {
        HRESULT hr = writeFactory->CreateTextFormat(
            family, nullptr, weight, DWRITE_FONT_STYLE_NORMAL,
            DWRITE_FONT_STRETCH_NORMAL, size, L"", result);
        if (SUCCEEDED(hr)) {
            (*result)->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);
            (*result)->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
        }
        return hr;
    }

    HRESULT CreateDeviceIndependentResources() {
        HRESULT hr = D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, &d2dFactory);
        if (FAILED(hr)) return hr;
        hr = DWriteCreateFactory(
            DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory),
            reinterpret_cast<IUnknown**>(&writeFactory));
        if (FAILED(hr)) return hr;
        hr = CreateTextFormat(design::Text16, DWRITE_FONT_WEIGHT_EXTRA_BOLD, &titleFormat);
        if (FAILED(hr)) return hr;
        hr = CreateTextFormat(design::Text13, DWRITE_FONT_WEIGHT_EXTRA_BOLD, &headingFormat);
        if (FAILED(hr)) return hr;
        hr = CreateTextFormat(design::Text12, DWRITE_FONT_WEIGHT_SEMI_BOLD, &bodyFormat);
        if (FAILED(hr)) return hr;
        hr = CreateTextFormat(design::Text11, DWRITE_FONT_WEIGHT_NORMAL, &smallFormat);
        if (FAILED(hr)) return hr;
        hr = CreateTextFormat(design::CardCountdown, DWRITE_FONT_WEIGHT_BLACK, &countdownFormat,
                              design::FontMono);
        if (FAILED(hr)) return hr;
        return CreateTextFormat(design::Text11, DWRITE_FONT_WEIGHT_BOLD, &buttonFormat);
    }

    void DiscardDeviceResources() noexcept {
        Release(brush);
        Release(textBrush);
        Release(secondaryBrush);
        Release(mutedBrush);
        Release(borderBrush);
        Release(goldBrush);
        Release(redBrush);
        Release(renderTarget);
    }

    HRESULT CreateDeviceResources() {
        if (renderTarget) return S_OK;
        RECT client{};
        GetClientRect(window, &client);
        const auto width = static_cast<UINT32>(std::max<LONG>(1, client.right - client.left));
        const auto height = static_cast<UINT32>(std::max<LONG>(1, client.bottom - client.top));
        HRESULT hr = d2dFactory->CreateHwndRenderTarget(
            D2D1::RenderTargetProperties(
                D2D1_RENDER_TARGET_TYPE_DEFAULT,
                D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_IGNORE)),
            D2D1::HwndRenderTargetProperties(window, D2D1::SizeU(width, height)),
            &renderTarget);
        if (FAILED(hr)) return hr;
        hr = renderTarget->CreateSolidColorBrush(design::Panel, &brush);
        if (SUCCEEDED(hr)) hr = renderTarget->CreateSolidColorBrush(design::TextPrimary, &textBrush);
        if (SUCCEEDED(hr)) hr = renderTarget->CreateSolidColorBrush(design::TextSecondary, &secondaryBrush);
        if (SUCCEEDED(hr)) hr = renderTarget->CreateSolidColorBrush(design::TextMuted, &mutedBrush);
        if (SUCCEEDED(hr)) hr = renderTarget->CreateSolidColorBrush(design::Border, &borderBrush);
        if (SUCCEEDED(hr)) hr = renderTarget->CreateSolidColorBrush(design::Gold, &goldBrush);
        if (SUCCEEDED(hr)) hr = renderTarget->CreateSolidColorBrush(design::Gold, &redBrush);
        if (FAILED(hr)) DiscardDeviceResources();
        return hr;
    }

    void SetBrush(ID2D1SolidColorBrush* target, D2D1_COLOR_F color) {
        target->SetColor(color);
    }

    void FillRect(const D2D1_RECT_F& rect, D2D1_COLOR_F color) {
        SetBrush(brush, color);
        renderTarget->FillRectangle(rect, brush);
    }

    void FillRounded(const D2D1_RECT_F& rect, float radius, D2D1_COLOR_F color) {
        SetBrush(brush, color);
        renderTarget->FillRoundedRectangle(D2D1::RoundedRect(rect, radius, radius), brush);
    }

    void FillRoundedGradient(const D2D1_RECT_F& rect, float radius,
                             D2D1_COLOR_F top, D2D1_COLOR_F bottom) {
        D2D1_GRADIENT_STOP stops[]{{0.0f, top}, {1.0f, bottom}};
        ID2D1GradientStopCollection* collection = nullptr;
        ID2D1LinearGradientBrush* gradient = nullptr;
        if (SUCCEEDED(renderTarget->CreateGradientStopCollection(
                stops, 2, D2D1_GAMMA_2_2, D2D1_EXTEND_MODE_CLAMP, &collection)) &&
            SUCCEEDED(renderTarget->CreateLinearGradientBrush(
                D2D1::LinearGradientBrushProperties(
                    D2D1::Point2F(rect.left, rect.top), D2D1::Point2F(rect.left, rect.bottom)),
                collection, &gradient))) {
            renderTarget->FillRoundedRectangle(D2D1::RoundedRect(rect, radius, radius), gradient);
        } else {
            FillRounded(rect, radius, bottom);
        }
        Release(gradient);
        Release(collection);
    }

    void FillRadial(const D2D1_RECT_F& rect, D2D1_POINT_2F center, float radiusX, float radiusY,
                    D2D1_COLOR_F inner, D2D1_COLOR_F outer, bool rounded = false) {
        D2D1_GRADIENT_STOP stops[]{{0.0f, inner}, {1.0f, outer}};
        ID2D1GradientStopCollection* collection = nullptr;
        ID2D1RadialGradientBrush* gradient = nullptr;
        if (SUCCEEDED(renderTarget->CreateGradientStopCollection(
                stops, 2, D2D1_GAMMA_2_2, D2D1_EXTEND_MODE_CLAMP, &collection)) &&
            SUCCEEDED(renderTarget->CreateRadialGradientBrush(
                D2D1::RadialGradientBrushProperties(center, D2D1::Point2F(0.0f, 0.0f), radiusX, radiusY),
                collection, &gradient))) {
            if (rounded) {
                renderTarget->FillRoundedRectangle(D2D1::RoundedRect(rect, design::Radius8, design::Radius8), gradient);
            } else {
                renderTarget->FillRectangle(rect, gradient);
            }
        }
        Release(gradient);
        Release(collection);
    }

    void StrokeRounded(const D2D1_RECT_F& rect, float radius, D2D1_COLOR_F color,
                       float width = 1.0f, bool dashed = false) {
        SetBrush(borderBrush, color);
        ID2D1StrokeStyle* strokeStyle = nullptr;
        if (dashed) {
            D2D1_STROKE_STYLE_PROPERTIES properties{};
            properties.dashStyle = D2D1_DASH_STYLE_DASH;
            d2dFactory->CreateStrokeStyle(properties, nullptr, 0, &strokeStyle);
        }
        renderTarget->DrawRoundedRectangle(
            D2D1::RoundedRect(rect, radius, radius), borderBrush, width, strokeStyle);
        Release(strokeStyle);
    }

    void Text(std::wstring_view value, IDWriteTextFormat* format, ID2D1SolidColorBrush* color,
              const D2D1_RECT_F& rect, DWRITE_TEXT_ALIGNMENT alignment = DWRITE_TEXT_ALIGNMENT_LEADING) {
        format->SetTextAlignment(alignment);
        renderTarget->DrawTextW(value.data(), static_cast<UINT32>(value.size()), format, rect, color);
    }

    void AddHit(D2D1_RECT_F rect, HitKind kind, Category category = Category::Helltide,
                int timer = -1, int value = 0) {
        hits.push_back(Hit{rect, kind, category, timer, value});
    }

    Hit* FindHit(float x, float y) {
        for (auto it = hits.rbegin(); it != hits.rend(); ++it) {
            if (Contains(it->rect, x, y)) return &*it;
        }
        return nullptr;
    }

    D2D1_RECT_F SliderRect(float x, float y, float width) const {
        return D2D1::RectF(x, y, x + std::max(90.0f, width), y + 16.0f);
    }

    void DrawSlider(const D2D1_RECT_F& rect, float value, bool enabled = true) {
        const float center = (rect.top + rect.bottom) * 0.5f;
        const float left = rect.left + 5.0f;
        const float right = rect.right - 5.0f;
        SetBrush(borderBrush, enabled ? D2D1::ColorF(0.32f, 0.32f, 0.32f, 1.0f)
                                     : D2D1::ColorF(0.20f, 0.20f, 0.20f, 1.0f));
        renderTarget->DrawLine(D2D1::Point2F(left, center), D2D1::Point2F(right, center), borderBrush, 3.0f);
        const float knob = left + (right - left) * Clamp01(value);
        SetBrush(goldBrush, enabled ? design::Gold : design::GoldDim);
        renderTarget->DrawLine(D2D1::Point2F(left, center), D2D1::Point2F(knob, center), goldBrush, 3.0f);
        renderTarget->FillEllipse(D2D1::Ellipse(D2D1::Point2F(knob, center), 5.0f, 5.0f), goldBrush);
    }

    void DrawToggle(D2D1_RECT_F rect, bool checked, bool enabled = true) {
        const auto fill = checked ? (enabled ? design::Gold : design::GoldDim)
                                  : D2D1::ColorF(0.15f, 0.15f, 0.15f, enabled ? 1.0f : 0.65f);
        FillRounded(rect, 7.0f, fill);
        StrokeRounded(rect, design::Radius6, enabled ? design::Border : design::BorderSubtle);
        const float x = checked ? rect.right - 8.0f : rect.left + 8.0f;
        SetBrush(secondaryBrush, checked ? D2D1::ColorF(0.1f, 0.07f, 0.02f, 1.0f)
                                         : D2D1::ColorF(0.70f, 0.70f, 0.70f, enabled ? 1.0f : 0.65f));
        renderTarget->FillEllipse(D2D1::Ellipse(D2D1::Point2F(x, (rect.top + rect.bottom) * 0.5f), 5.0f, 5.0f), secondaryBrush);
    }

    void DrawCheckbox(float x, float y, bool checked, bool enabled = true) {
        const D2D1_RECT_F box = D2D1::RectF(x, y, x + 14.0f, y + 14.0f);
        FillRounded(box, 3.0f, checked ? (enabled ? design::Gold : design::GoldDim)
                                        : D2D1::ColorF(0.08f, 0.08f, 0.08f, enabled ? 1.0f : 0.6f));
        StrokeRounded(box, 3.0f, enabled ? design::Border : design::BorderSubtle);
        if (checked) {
            SetBrush(secondaryBrush, D2D1::ColorF(0.10f, 0.07f, 0.02f, 1.0f));
            renderTarget->DrawLine(D2D1::Point2F(x + 3.0f, y + 7.0f),
                                   D2D1::Point2F(x + 6.0f, y + 10.0f), secondaryBrush, 1.6f);
            renderTarget->DrawLine(D2D1::Point2F(x + 6.0f, y + 10.0f),
                                   D2D1::Point2F(x + 12.0f, y + 3.0f), secondaryBrush, 1.6f);
        }
    }

    void DrawButton(const D2D1_RECT_F& rect, std::wstring_view label, bool primary = false,
                    bool enabled = true) {
        FillRounded(rect, 5.0f,
                    enabled ? (primary ? design::WithAlpha(design::Gold, 0.24f)
                                       : D2D1::ColorF(0.10f, 0.10f, 0.10f, 0.96f))
                            : D2D1::ColorF(0.06f, 0.06f, 0.06f, 0.72f));
        StrokeRounded(rect, 5.0f,
                      enabled ? (primary ? design::Gold : design::Border) : design::BorderSubtle);
        Text(label, buttonFormat, enabled ? (primary ? textBrush : secondaryBrush) : mutedBrush,
             rect, DWRITE_TEXT_ALIGNMENT_CENTER);
    }

    void DrawCategoryCard(Category category, const CategoryView& categoryView,
                          const D2D1_RECT_F& card, bool compact) {
        (void)compact;
        const D2D1_COLOR_F accent = CategoryColor(category);
        const std::array<const wchar_t*, 3> defaults{L"Helltide", L"Legion", L"World Boss"};
        const std::wstring_view title = categoryView.title.empty() ? defaults[static_cast<std::size_t>(IndexOf(category))]
                                                                    : std::wstring_view(categoryView.title);
        const bool expanded = categoryView.enabled && categoryView.expanded;
        const float headerHeight = expanded ? 60.0f : 84.0f;

        FillRoundedGradient(card, design::Radius8,
                            categoryView.enabled ? D2D1::ColorF(0.055f, 0.055f, 0.055f, 0.98f)
                                                  : D2D1::ColorF(0.030f, 0.030f, 0.030f, 0.88f),
                            D2D1::ColorF(0.0f, 0.0f, 0.0f, categoryView.enabled ? 0.98f : 0.92f));
        FillRadial(card,
                   D2D1::Point2F(card.left + (card.right - card.left) * 0.10f, card.top),
                   std::max(180.0f, (card.right - card.left) * 0.85f), 120.0f,
                   D2D1::ColorF(accent.r, accent.g, accent.b, categoryView.enabled ? 0.18f : 0.05f),
                   D2D1::ColorF(accent.r, accent.g, accent.b, 0.0f), true);
        StrokeRounded(card, design::Radius8,
                      D2D1::ColorF(accent.r, accent.g, accent.b, categoryView.enabled ? 0.42f : 0.22f),
                      1.0f, !categoryView.enabled);

        const D2D1_RECT_F headerFill = D2D1::RectF(card.left + 1.0f, card.top + 1.0f,
                                                  card.right - 1.0f, card.top + headerHeight - 1.0f);
        FillRoundedGradient(headerFill, design::Radius6,
                            D2D1::ColorF(0.047f, 0.039f, 0.039f, categoryView.enabled ? 0.62f : 0.38f),
                            D2D1::ColorF(0.0f, 0.0f, 0.0f, 0.35f));
        SetBrush(borderBrush, D2D1::ColorF(design::Gold.r, design::Gold.g, design::Gold.b, 0.15f));
        renderTarget->DrawLine(D2D1::Point2F(card.left + 1.0f, card.top + headerHeight - 1.0f),
                               D2D1::Point2F(card.right - 1.0f, card.top + headerHeight - 1.0f),
                               borderBrush, 1.0f);

        const float stackWidth = std::min(142.0f, std::max(118.0f, (card.right - card.left) * 0.40f));
        const float stackLeft = card.right - 8.0f - stackWidth;
        const float metaRight = stackLeft - 8.0f;
        const float metaLeft = metaRight - 86.0f;
        const float disclosureLeft = card.left + 8.0f;
        const float titleLeft = disclosureLeft + 18.0f;
        const float titleRight = metaLeft - 8.0f;
        const D2D1_RECT_F disclosure = D2D1::RectF(disclosureLeft, card.top + 12.0f,
                                                   disclosureLeft + 14.0f, card.top + 30.0f);
        Text(expanded ? L"▾" : L"▸", buttonFormat, goldBrush, disclosure, DWRITE_TEXT_ALIGNMENT_CENTER);
        const D2D1_RECT_F cardHeaderHit = D2D1::RectF(card.left + 4.0f, card.top + 4.0f,
                                                     std::max(card.left + 5.0f, stackLeft - 2.0f),
                                                     card.top + headerHeight - 4.0f);
        if (categoryView.enabled) AddHit(cardHeaderHit, HitKind::CardHeader, category);

        Text(title, headingFormat, categoryView.enabled ? textBrush : mutedBrush,
             D2D1::RectF(titleLeft, card.top + 8.0f, titleRight, card.top + 25.0f));
        if (!categoryView.subtitle.empty()) {
            Text(categoryView.subtitle, smallFormat, categoryView.enabled ? secondaryBrush : mutedBrush,
                 D2D1::RectF(titleLeft, card.top + 25.0f, titleRight, card.top + 40.0f));
        }

        const wchar_t* metaLabel = categoryView.active ? L"ENDET" : L"IN";
        // Keep label and countdown on separate rows inside the 84px source header.
        Text(metaLabel, smallFormat, mutedBrush,
             D2D1::RectF(metaLeft, card.top + 40.0f, metaRight, card.top + 51.0f),
             DWRITE_TEXT_ALIGNMENT_TRAILING);
        Text(categoryView.countdown.empty() ? L"—" : categoryView.countdown, countdownFormat,
             categoryView.enabled ? goldBrush : mutedBrush,
             D2D1::RectF(metaLeft, card.top + 52.0f, metaRight, card.top + 79.0f),
             DWRITE_TEXT_ALIGNMENT_TRAILING);

        const float statusTop = card.top + 9.0f;
        const D2D1_RECT_F status = D2D1::RectF(stackLeft, statusTop, stackLeft + 66.0f, statusTop + 19.0f);
        FillRounded(status, 999.0f, categoryView.enabled ? D2D1::ColorF(design::Gold.r, design::Gold.g, design::Gold.b, 0.12f)
                                                          : D2D1::ColorF(1.0f, 1.0f, 1.0f, 0.04f));
        StrokeRounded(status, 999.0f,
                      categoryView.enabled ? D2D1::ColorF(design::Gold.r, design::Gold.g, design::Gold.b, 0.35f)
                                            : D2D1::ColorF(1.0f, 1.0f, 1.0f, 0.12f));
        Text(categoryView.enabled ? L"AKTIV" : L"PAUSIERT", smallFormat,
             categoryView.enabled ? textBrush : mutedBrush, status, DWRITE_TEXT_ALIGNMENT_CENTER);
        Text(categoryView.eventTime.empty() ? L"—" : categoryView.eventTime, smallFormat, secondaryBrush,
             D2D1::RectF(stackLeft + 72.0f, statusTop, card.right - 8.0f, statusTop + 19.0f),
             DWRITE_TEXT_ALIGNMENT_TRAILING);

        const float rememberTop = expanded ? card.top + 33.0f : card.top + 52.0f;
        DrawCheckbox(stackLeft, rememberTop, categoryView.enabled);
        Text(L"Erinnern", smallFormat, categoryView.enabled ? secondaryBrush : mutedBrush,
             D2D1::RectF(stackLeft + 20.0f, rememberTop - 1.0f, card.right - 8.0f, rememberTop + 16.0f));
        AddHit(D2D1::RectF(stackLeft - 4.0f, rememberTop - 4.0f, card.right - 4.0f, rememberTop + 19.0f),
               HitKind::CategoryEnabled, category);

        if (!expanded) return;
        const float bodyTop = card.top + 62.0f;
        const float rowHeight = 48.0f;
        Text(L"Reminder", smallFormat, mutedBrush,
             D2D1::RectF(card.left + 14.0f, bodyTop, card.right - 14.0f, bodyTop + 16.0f));

        const float countTop = bodyTop + 18.0f;
        Text(L"Timer", smallFormat, mutedBrush,
             D2D1::RectF(card.left + 14.0f, countTop, card.left + 55.0f, countTop + 18.0f));
        for (int i = 0; i < 3; ++i) {
            const float left = card.left + 58.0f + static_cast<float>(i) * 31.0f;
            const D2D1_RECT_F button = D2D1::RectF(left, countTop + 1.0f, left + 25.0f, countTop + 19.0f);
            DrawButton(button, std::to_wstring(i + 1), categoryView.timerCount == i + 1);
            AddHit(button, HitKind::TimerCount, category, -1, i + 1);
        }

        for (int i = 0; i < std::clamp(categoryView.timerCount, 1, 3); ++i) {
            const float top = countTop + 25.0f + static_cast<float>(i) * rowHeight;
            const auto& timer = categoryView.timers[static_cast<std::size_t>(i)];
            Text(std::wstring(L"T") + std::to_wstring(i + 1), smallFormat, mutedBrush,
                 D2D1::RectF(card.left + 14.0f, top, card.left + 32.0f, top + 17.0f));
            const D2D1_RECT_F minutes = SliderRect(card.left + 34.0f, top + 1.0f, std::max(80.0f, card.right - card.left - 132.0f));
            DrawSlider(minutes, static_cast<float>(std::clamp(timer.minutesBefore, 0, 60)) / 60.0f,
                       categoryView.enabled);
            AddHit(minutes, HitKind::TimerMinutes, category, i);
            Text(std::to_wstring(std::clamp(timer.minutesBefore, 0, 60)) + L" min", smallFormat,
                 secondaryBrush, D2D1::RectF(minutes.left, top + 11.0f, minutes.right, top + 28.0f),
                 DWRITE_TEXT_ALIGNMENT_CENTER);

            const D2D1_RECT_F tts = D2D1::RectF(card.right - 82.0f, top, card.right - 54.0f, top + 14.0f);
            DrawCheckbox(tts.left, tts.top, timer.ttsEnabled, categoryView.enabled);
            Text(L"TTS", smallFormat, mutedBrush, D2D1::RectF(tts.right + 4.0f, top - 1.0f, card.right - 14.0f, top + 15.0f));
            AddHit(D2D1::RectF(tts.left - 2.0f, tts.top - 3.0f, card.right - 12.0f, tts.bottom + 4.0f),
                   HitKind::TimerTts, category, i);

            const D2D1_RECT_F pitch = SliderRect(card.left + 34.0f, top + 28.0f, std::max(80.0f, card.right - card.left - 132.0f));
            DrawSlider(pitch, static_cast<float>(std::clamp(timer.pitchHz, 200, 2000) - 200) / 1800.0f,
                       categoryView.enabled);
            AddHit(pitch, HitKind::TimerPitch, category, i);
            Text(std::to_wstring(std::clamp(timer.pitchHz, 200, 2000)) + L" Hz", smallFormat,
                 secondaryBrush, D2D1::RectF(pitch.left, top + 38.0f, pitch.right, top + 54.0f),
                 DWRITE_TEXT_ALIGNMENT_CENTER);
            const D2D1_RECT_F beep = D2D1::RectF(card.right - 83.0f, top + 28.0f, card.right - 14.0f, top + 49.0f);
            DrawButton(beep, timer.beepPattern == BeepPattern::Beep ? L"Beep" : timer.beepPattern == BeepPattern::Double ? L"Double" : L"Triple");
            AddHit(beep, HitKind::TimerBeep, category, i);
        }
    }

    void BuildMainLayout(float width, float height) {
        hits.clear();
        const float gap = 6.0f;
        const float containerWidth = std::min(560.0f, std::max(16.0f, width));
        const float contentWidth = containerWidth - 16.0f;
        const float contentLeft = (width - containerWidth) * 0.5f + 8.0f;
        const float cardWidth = contentWidth;
        const float cardBaseHeight = 84.0f;
        std::array<int, 3> order{0, 1, 2};
        std::stable_sort(order.begin(), order.end(), [&](int left, int right) {
            const auto& a = state.categories[static_cast<std::size_t>(left)];
            const auto& b = state.categories[static_cast<std::size_t>(right)];
            if (a.enabled != b.enabled) return a.enabled > b.enabled;
            if (!a.enabled) return false;
            return a.targetMs < b.targetMs;
        });

        float y = 70.0f;
        for (const int index : order) {
            const auto& category = state.categories[static_cast<std::size_t>(index)];
            const float cardHeight = category.enabled && category.expanded ? 278.0f : cardBaseHeight;
            DrawCategoryCard(CategoryAt(index), category,
                             D2D1::RectF(contentLeft, y, contentLeft + cardWidth, y + cardHeight), true);
            y += cardHeight + gap;
        }

        // Source keeps overlay controls fixed to the viewport, outside the card flow.
        const float overlayWidth = 136.0f;
        const float overlayHeight = 34.0f;
        const float overlayRight = width - 8.0f;
        const float overlayLeft = std::max(8.0f, overlayRight - overlayWidth);
        const float overlayTop = std::max(8.0f, height - 8.0f - overlayHeight);
        const D2D1_RECT_F overlay = D2D1::RectF(overlayLeft, overlayTop, overlayRight, overlayTop + overlayHeight);
        FillRoundedGradient(overlay, design::Radius8,
                            D2D1::ColorF(0.047f, 0.047f, 0.047f, 0.96f),
                            D2D1::ColorF(0.0f, 0.0f, 0.0f, 0.96f));
        StrokeRounded(overlay, design::Radius8, design::Border);
        DrawCheckbox(overlay.left + 8.0f, overlay.top + 10.0f, state.overlayEnabled, !state.panicStop);
        Text(L"Overlay", smallFormat, secondaryBrush,
             D2D1::RectF(overlay.left + 28.0f, overlay.top + 8.0f, overlay.left + 82.0f, overlay.bottom - 8.0f));
        if (!state.panicStop) {
            AddHit(D2D1::RectF(overlay.left + 4.0f, overlay.top + 3.0f,
                               overlay.left + 84.0f, overlay.bottom - 3.0f),
                   HitKind::OverlayEnabled);
        }
        const D2D1_RECT_F position = D2D1::RectF(overlay.right - 54.0f, overlay.top + 5.0f,
                                                  overlay.right - 8.0f, overlay.bottom - 5.0f);
        const bool positionEnabled = !state.panicStop && state.overlayEnabled;
        DrawButton(position, L"Position", false, positionEnabled);
        if (positionEnabled) AddHit(position, HitKind::BeginOverlayMove);
    }

    void DrawHeader(float width) {
        const float containerWidth = std::min(560.0f, std::max(16.0f, width));
        const float contentWidth = containerWidth - 16.0f;
        const float left = (width - containerWidth) * 0.5f + 8.0f;
        const D2D1_RECT_F header = D2D1::RectF(left, 8.0f, left + contentWidth, 62.0f);
        FillRoundedGradient(header, design::Radius8,
                            D2D1::ColorF(0.055f, 0.055f, 0.055f, 0.98f),
                            D2D1::ColorF(0.025f, 0.025f, 0.025f, 0.98f));
        StrokeRounded(header, design::Radius8, design::Border);
        SetBrush(goldBrush, D2D1::ColorF(design::Gold.r, design::Gold.g, design::Gold.b, 0.12f));
        renderTarget->DrawLine(D2D1::Point2F(header.left + 1.0f, header.top + 1.0f),
                               D2D1::Point2F(header.right - 1.0f, header.top + 1.0f), goldBrush, 1.0f);
        Text(L"hell", titleFormat, textBrush, D2D1::RectF(header.left + 8.0f, header.top + 6.0f,
                                                          header.left + 54.0f, header.top + 29.0f));
        SetBrush(redBrush, design::Legion);
        Text(L"time", titleFormat, redBrush, D2D1::RectF(header.left + 45.0f, header.top + 6.0f,
                                                         header.left + 92.0f, header.top + 29.0f));
        Text(L"Event Timers", smallFormat, mutedBrush, D2D1::RectF(header.left + 8.0f, header.top + 34.0f,
                                                                  header.left + 130.0f, header.bottom - 6.0f));
        const D2D1_RECT_F settings = D2D1::RectF(header.right - 48.0f, header.top + 9.0f,
                                                 header.right - 10.0f, header.top + 39.0f);
        DrawButton(settings, L"\u2699");
        AddHit(settings, HitKind::Settings);
    }

    void DrawModalSection(const D2D1_RECT_F& section) {
        FillRounded(section, 7.0f, D2D1::ColorF(0.04f, 0.04f, 0.04f, 0.96f));
        StrokeRounded(section, 7.0f, design::Border);
    }

    void DrawModal(float width, float height) {
        FillRect(D2D1::RectF(0.0f, 0.0f, width, height), D2D1::ColorF(0.0f, 0.0f, 0.0f, 0.65f));
        const float modalWidth = std::min(680.0f, std::max(280.0f, width - 16.0f));
        const float left = (width - modalWidth) * 0.5f;
        const float top = 10.0f;
        const float bottom = std::max(top + 260.0f, std::min(height - 10.0f, top + 640.0f));
        const D2D1_RECT_F modal = D2D1::RectF(left, top, left + modalWidth, bottom);
        FillRounded(modal, 10.0f, D2D1::ColorF(0.015f, 0.015f, 0.015f, 0.99f));
        StrokeRounded(modal, 10.0f, design::Gold);
        AddHit(modal, HitKind::ModalPanel);
        Text(L"Einstellungen", headingFormat, textBrush,
             D2D1::RectF(left + 14.0f, top + 8.0f, modal.right - 56.0f, top + 38.0f));
        const D2D1_RECT_F close = D2D1::RectF(modal.right - 44.0f, top + 8.0f, modal.right - 12.0f, top + 38.0f);
        DrawButton(close, L"X");
        AddHit(close, HitKind::CloseSettings);

        float y = top + 48.0f;
        const float pad = 12.0f;
        const float controlLeft = left + pad;
        const float right = modal.right - pad;

        const D2D1_RECT_F behavior = D2D1::RectF(controlLeft, y, right, y + 140.0f);
        DrawModalSection(behavior);
        Text(L"Overlay Verhalten", headingFormat, textBrush, D2D1::RectF(behavior.left + 12.0f, y + 7.0f, right - 12.0f, y + 27.0f));
        Text(L"Aktiv", smallFormat, mutedBrush, D2D1::RectF(behavior.left + 12.0f, y + 32.0f, behavior.left + 58.0f, y + 52.0f));
        const D2D1_RECT_F overlayToggle = D2D1::RectF(behavior.left + 66.0f, y + 35.0f, behavior.left + 92.0f, y + 49.0f);
        DrawToggle(overlayToggle, state.overlayEnabled, !state.panicStop);
        AddHit(D2D1::RectF(behavior.left + 60.0f, y + 28.0f, behavior.left + 100.0f, y + 56.0f), HitKind::OverlayEnabled);
        Text(L"Inhalt", smallFormat, mutedBrush, D2D1::RectF(behavior.left + 116.0f, y + 32.0f, behavior.left + 160.0f, y + 52.0f));
        const D2D1_RECT_F overview = D2D1::RectF(behavior.left + 166.0f, y + 30.0f, behavior.left + 240.0f, y + 56.0f);
        const D2D1_RECT_F toast = D2D1::RectF(behavior.left + 244.0f, y + 30.0f, behavior.left + 300.0f, y + 56.0f);
        DrawButton(overview, L"Overview", state.overlayMode == OverlayMode::Overview);
        DrawButton(toast, L"Toast", state.overlayMode == OverlayMode::Toast);
        AddHit(overview, HitKind::OverlayMode, Category::Helltide, -1, 0);
        AddHit(toast, HitKind::OverlayMode, Category::Helltide, -1, 1);
        Text(L"Kategorien", smallFormat, mutedBrush, D2D1::RectF(behavior.left + 12.0f, y + 67.0f, behavior.left + 76.0f, y + 88.0f));
        const std::array<const wchar_t*, 3> labels{L"Helltide", L"Legion", L"World Boss"};
        float x = behavior.left + 82.0f;
        for (int i = 0; i < 3; ++i) {
            DrawCheckbox(x, y + 70.0f, state.overlayCategories[static_cast<std::size_t>(i)], !state.panicStop && state.overlayEnabled);
            Text(labels[static_cast<std::size_t>(i)], smallFormat, mutedBrush, D2D1::RectF(x + 19.0f, y + 67.0f, x + 92.0f, y + 88.0f));
            AddHit(D2D1::RectF(x - 3.0f, y + 64.0f, x + 95.0f, y + 92.0f), HitKind::OverlayCategory, CategoryAt(i));
            x += i == 0 ? 99.0f : 106.0f;
        }
        const D2D1_RECT_F preview = D2D1::RectF(behavior.left + 12.0f, y + 102.0f, behavior.left + 86.0f, y + 128.0f);
        const D2D1_RECT_F position = D2D1::RectF(behavior.left + 92.0f, y + 102.0f, behavior.left + 184.0f, y + 128.0f);
        const D2D1_RECT_F resetPosition = D2D1::RectF(behavior.left + 190.0f, y + 102.0f, behavior.left + 294.0f, y + 128.0f);
        const bool overlayControlsEnabled = state.overlayEnabled && !state.panicStop;
        DrawButton(preview, L"Vorschau", overlayControlsEnabled);
        DrawButton(position, L"Position", overlayControlsEnabled);
        DrawButton(resetPosition, L"Reset Position", overlayControlsEnabled);
        if (overlayControlsEnabled) {
            AddHit(preview, HitKind::PreviewOverlay);
            AddHit(position, HitKind::BeginOverlayMove);
            AddHit(resetPosition, HitKind::ResetOverlayPosition);
        }
        y += 148.0f;

        const D2D1_RECT_F look = D2D1::RectF(controlLeft, y, right, y + 150.0f);
        DrawModalSection(look);
        Text(L"Overlay Look", headingFormat, textBrush, D2D1::RectF(look.left + 12.0f, y + 7.0f, right - 12.0f, y + 27.0f));
        const auto addSlider = [&](std::wstring_view label, std::wstring_view value, float normalized,
                                   HitKind kind, float topY) {
            Text(label, smallFormat, mutedBrush, D2D1::RectF(look.left + 12.0f, topY, look.left + 170.0f, topY + 17.0f));
            Text(value, smallFormat, secondaryBrush, D2D1::RectF(right - 58.0f, topY, right - 12.0f, topY + 17.0f), DWRITE_TEXT_ALIGNMENT_TRAILING);
            const auto slider = SliderRect(look.left + 174.0f, topY + 1.0f, std::max(65.0f, right - look.left - 250.0f));
            DrawSlider(slider, normalized, !state.panicStop);
            AddHit(slider, kind);
        };
        addSlider(L"Breite", std::to_wstring(static_cast<int>(std::round(state.overlayScaleX * 100.0f))) + L"%",
                  (ClampScale(state.overlayScaleX) - 0.6f) / 1.4f, HitKind::OverlayScaleX, y + 31.0f);
        addSlider(L"Höhe", std::to_wstring(static_cast<int>(std::round(state.overlayScaleY * 100.0f))) + L"%",
                  (ClampScale(state.overlayScaleY) - 0.6f) / 1.4f, HitKind::OverlayScaleY, y + 64.0f);
        addSlider(L"Transparenz", std::to_wstring(static_cast<int>(std::round(Clamp01(state.overlayOpacity) * 100.0f))) + L"%",
                  Clamp01(state.overlayOpacity), HitKind::OverlayOpacity, y + 97.0f);
        y += 158.0f;

        const D2D1_RECT_F behavior2 = D2D1::RectF(controlLeft, y, right, y + 94.0f);
        DrawModalSection(behavior2);
        Text(L"Erinnerungen", headingFormat, textBrush, D2D1::RectF(behavior2.left + 12.0f, y + 7.0f, right - 12.0f, y + 27.0f));
        const auto addFlag = [&](std::wstring_view label, bool checked, HitKind kind, float leftX) {
            DrawCheckbox(leftX, y + 38.0f, checked, !state.panicStop);
            Text(label, smallFormat, mutedBrush, D2D1::RectF(leftX + 19.0f, y + 35.0f, leftX + 116.0f, y + 56.0f));
            AddHit(D2D1::RectF(leftX - 4.0f, y + 31.0f, leftX + 120.0f, y + 62.0f), kind);
        };
        addFlag(L"Ton", state.soundEnabled, HitKind::SoundEnabled, behavior2.left + 12.0f);
        addFlag(L"Auto", state.autoRefreshEnabled, HitKind::AutoRefreshEnabled, behavior2.left + 135.0f);
        addFlag(L"Toasts", state.systemToastsEnabled, HitKind::SystemToastsEnabled, behavior2.left + 258.0f);
        Text(L"Lautstärke", smallFormat, mutedBrush, D2D1::RectF(behavior2.left + 12.0f, y + 64.0f, behavior2.left + 80.0f, y + 82.0f));
        const auto volume = SliderRect(behavior2.left + 86.0f, y + 65.0f, std::max(65.0f, right - behavior2.left - 160.0f));
        DrawSlider(volume, Clamp01(state.volume), !state.panicStop);
        AddHit(volume, HitKind::Volume);
        if (state.panicStop) {
            const D2D1_RECT_F reset = D2D1::RectF(right - 92.0f, y + 65.0f, right - 12.0f, y + 88.0f);
            DrawButton(reset, L"Reset", true);
            AddHit(reset, HitKind::PanicReset);
        }

        const D2D1_RECT_F debugToggle = D2D1::RectF(controlLeft, y + 102.0f, controlLeft + 70.0f, y + 126.0f);
        DrawButton(debugToggle, L"Debug", overlayDebugOpen);
        AddHit(debugToggle, HitKind::ToggleOverlayDebug);
        if (overlayDebugOpen) {
            const float debugTop = y + 134.0f;
            const float debugBottom = std::min(modal.bottom - 8.0f, debugTop + 148.0f);
            if (debugBottom > debugTop + 58.0f) {
                const D2D1_RECT_F debug = D2D1::RectF(controlLeft, debugTop, right, debugBottom);
                DrawModalSection(debug);
                Text(L"Overlay Debug", headingFormat, textBrush,
                     D2D1::RectF(debug.left + 12.0f, debug.top + 5.0f, debug.right - 12.0f, debug.top + 23.0f));
                const D2D1_RECT_F refresh = D2D1::RectF(debug.left + 12.0f, debug.top + 26.0f, debug.left + 106.0f, debug.top + 48.0f);
                const D2D1_RECT_F clear = D2D1::RectF(debug.left + 112.0f, debug.top + 26.0f, debug.left + 204.0f, debug.top + 48.0f);
                DrawButton(refresh, L"Overlay Status");
                DrawButton(clear, L"Logs leeren");
                AddHit(refresh, HitKind::RefreshOverlayDiagnostics);
                AddHit(clear, HitKind::ClearOverlayDiagnostics);

                const auto& diag = state.overlayDiagnostics;
                const auto onOff = [](bool value) -> const wchar_t* { return value ? L"ja" : L"nein"; };
                const auto mode = diag.mode == OverlayMode::Toast ? L"Toast" : L"Overview";
                const float lineTop = debug.top + 52.0f;
                Text(L"HWND=" + std::to_wstring(static_cast<unsigned long long>(diag.hwnd)) +
                         L" existiert=" + onOff(diag.exists) + L" sichtbar=" + onOff(diag.visible) +
                         L" show=" + onOff(diag.showSucceeded) + L" pos=" + onOff(diag.positioning) + L" Modus=" + mode,
                     smallFormat, mutedBrush, D2D1::RectF(debug.left + 12.0f, lineTop, debug.right - 12.0f, lineTop + 12.0f));
                Text(L"Bounds=" + std::to_wstring(diag.bounds.left) + L"," + std::to_wstring(diag.bounds.top) +
                         L" " + std::to_wstring(diag.bounds.right - diag.bounds.left) + L"x" +
                         std::to_wstring(diag.bounds.bottom - diag.bounds.top) + L" Frame=" + onOff(diag.renderSucceeded) +
                         L" UWL=" + onOff(diag.updateLayeredWindowSucceeded),
                     smallFormat, mutedBrush, D2D1::RectF(debug.left + 12.0f, lineTop + 13.0f, debug.right - 12.0f, lineTop + 25.0f));
                Text(L"Alpha=" + std::to_wstring(diag.nonZeroAlphaPixels) + L" RGB=" +
                         std::to_wstring(diag.nonZeroColorPixels) + L" maxA=" + std::to_wstring(diag.maxAlpha) +
                         L" frameAlpha=" + onOff(diag.frameHadAlpha) + L" frameTick=" +
                         std::to_wstring(diag.lastSuccessfulFrameTick),
                     smallFormat, mutedBrush, D2D1::RectF(debug.left + 12.0f, lineTop + 26.0f, debug.right - 12.0f, lineTop + 38.0f));
                Text(std::wstring{L"Gates P="} + onOff(diag.gatePanicStop) + L" O=" + onOff(diag.gateOverlayEnabled) +
                         L" S=" + onOff(diag.gateSettingsEnabled) + L" rows=" + onOff(diag.gateOverviewRows) +
                         L" toast=" + onOff(diag.gateToastMode) + L" reminder=" + onOff(diag.gateReminderActive),
                     smallFormat, mutedBrush, D2D1::RectF(debug.left + 12.0f, lineTop + 39.0f, debug.right - 12.0f, lineTop + 51.0f));
                Text(L"Win32=" + std::to_wstring(diag.lastWin32Error) + L" HRESULT=" +
                         std::to_wstring(static_cast<long>(diag.lastHresult)) + L" " + diag.lastError,
                     smallFormat, diag.lastError.empty() ? mutedBrush : textBrush,
                     D2D1::RectF(debug.left + 12.0f, lineTop + 52.0f, debug.right - 12.0f, lineTop + 64.0f));
                const float eventsTop = lineTop + 66.0f;
                for (std::size_t index = 0; index < diag.recentEventCount && index < diag.recentEvents.size(); ++index) {
                    const int column = static_cast<int>(index % 2);
                    const int row = static_cast<int>(index / 2);
                    const float eventLeft = debug.left + 12.0f + column * ((debug.right - debug.left - 30.0f) * 0.5f);
                    const float eventRight = column == 0 ? debug.left + (debug.right - debug.left) * 0.5f : debug.right - 12.0f;
                    Text(std::to_wstring(index + 1) + L". " + diag.recentEvents[index], smallFormat, mutedBrush,
                         D2D1::RectF(eventLeft, eventsTop + row * 12.0f, eventRight, eventsTop + row * 12.0f + 11.0f));
                }
            }
        }

        // Panel hit is registered before controls, so controls win reverse hit-test order.
    }

    int PreferredMainClientHeight(int clientWidth) const noexcept {
        (void)clientWidth;
        constexpr float gap = 6.0f;
        constexpr float headerTop = 8.0f;
        constexpr float headerHeight = 54.0f;
        constexpr float gridTop = headerTop + headerHeight + 8.0f;
        constexpr float cardHeight = 84.0f;
        float contentBottom = gridTop;
        std::array<int, 3> order{0, 1, 2};
        std::stable_sort(order.begin(), order.end(), [&](int left, int right) {
            const auto& a = state.categories[static_cast<std::size_t>(left)];
            const auto& b = state.categories[static_cast<std::size_t>(right)];
            if (a.enabled != b.enabled) return a.enabled > b.enabled;
            if (!a.enabled) return false;
            return a.targetMs < b.targetMs;
        });
        for (std::size_t position = 0; position < order.size(); ++position) {
            const int index = order[position];
            const auto& category = state.categories[static_cast<std::size_t>(index)];
            contentBottom += (category.enabled && category.expanded) ? 278.0f : cardHeight;
            if (position + 1 < order.size()) contentBottom += gap;
        }
        // Source adds fixed overlay clearance and clamps the logical window height.
        const float desired = contentBottom + 8.0f + 34.0f + 36.0f;
        return static_cast<int>(std::clamp(std::lround(desired), 360L, 980L));
    }

    void Render() {
        if (!window || FAILED(CreateDeviceResources())) return;
        RECT client{};
        GetClientRect(window, &client);
        const float width = static_cast<float>(std::max<LONG>(1, client.right - client.left));
        const float height = static_cast<float>(std::max<LONG>(1, client.bottom - client.top));
        renderTarget->BeginDraw();
        renderTarget->Clear(design::Background);
        FillRadial(D2D1::RectF(0.0f, 0.0f, width, std::min(height, 260.0f)),
                   D2D1::Point2F(width * 0.5f, 0.0f), std::max(160.0f, width * 0.90f), 260.0f,
                   design::WithAlpha(design::Burgundy, 0.24f), D2D1::ColorF(0.0f, 0.0f, 0.0f, 0.0f));
        FillRadial(D2D1::RectF(0.0f, std::max(0.0f, height - 240.0f), width, height),
                   D2D1::Point2F(width * 0.5f, height), std::max(180.0f, width * 0.75f), 240.0f,
                   design::WithAlpha(design::BurgundyDark, 0.18f), D2D1::ColorF(0.0f, 0.0f, 0.0f, 0.0f));
        for (float scanline = 0.0f; scanline < height; scanline += 4.0f) {
            FillRect(D2D1::RectF(0.0f, scanline + 2.0f, width, scanline + 4.0f),
                     D2D1::ColorF(0.0f, 0.0f, 0.0f, 0.035f));
        }
        hits.clear();
        if (settingsOpen) {
            DrawModal(width, height);
        } else {
            DrawHeader(width);
            BuildMainLayout(width, height);
            if (state.panicStop) {
                const D2D1_RECT_F warning = D2D1::RectF(12.0f, std::min(height - 38.0f, 60.0f), width - 72.0f, std::min(height - 10.0f, 86.0f));
                FillRounded(warning, 6.0f, D2D1::ColorF(0.20f, 0.08f, 0.03f, 0.94f));
                StrokeRounded(warning, 6.0f, design::GoldDim);
                Text(L"Sicherheits-Stopp aktiv", smallFormat, textBrush, D2D1::RectF(warning.left + 10.0f, warning.top + 2.0f, warning.right, warning.bottom - 2.0f));
                const D2D1_RECT_F reset = D2D1::RectF(width - 58.0f, warning.top + 3.0f, width - 12.0f, warning.bottom - 3.0f);
                DrawButton(reset, L"Reset", true);
                AddHit(reset, HitKind::PanicReset);
            }
        }
        const HRESULT hr = renderTarget->EndDraw();
        if (hr == D2DERR_RECREATE_TARGET) DiscardDeviceResources();
    }

    void UpdateSlider(float x) {
        RECT client{};
        GetClientRect(window, &client);
        const float width = static_cast<float>(client.right - client.left);
        const float modalWidth = std::min(680.0f, std::max(280.0f, width - 16.0f));
        const float left = (width - modalWidth) * 0.5f;
        (void)left;
        const auto setSlider = [&](HitKind kind, float normalized) {
            normalized = Clamp01(normalized);
            UiAction action{};
            action.kind = ActionKind::SetOverlayOpacity;
            switch (kind) {
            case HitKind::OverlayScaleX:
                action.kind = ActionKind::SetOverlayScaleX;
                action.value = 0.6f + normalized * 1.4f;
                state.overlayScaleX = action.value;
                break;
            case HitKind::OverlayScaleY:
                action.kind = ActionKind::SetOverlayScaleY;
                action.value = 0.6f + normalized * 1.4f;
                state.overlayScaleY = action.value;
                break;
            case HitKind::OverlayOpacity:
                action.kind = ActionKind::SetOverlayOpacity;
                action.value = normalized;
                state.overlayOpacity = normalized;
                break;
            case HitKind::Volume:
                action.kind = ActionKind::SetVolume;
                action.value = normalized;
                state.volume = normalized;
                break;
            case HitKind::TimerMinutes: {
                action.kind = ActionKind::SetTimerMinutes;
                action.category = draggingCategory;
                action.timerIndex = draggingTimer;
                action.intValue = std::clamp(static_cast<int>(std::lround(normalized * 60.0f)), 0, 60);
                state.categories[static_cast<std::size_t>(IndexOf(draggingCategory))].timers[static_cast<std::size_t>(draggingTimer)].minutesBefore = action.intValue;
                break;
            }
            case HitKind::TimerPitch: {
                action.kind = ActionKind::SetTimerPitchHz;
                action.category = draggingCategory;
                action.timerIndex = draggingTimer;
                action.intValue = std::clamp(static_cast<int>(std::lround(200.0f + normalized * 1800.0f)), 200, 2000);
                state.categories[static_cast<std::size_t>(IndexOf(draggingCategory))].timers[static_cast<std::size_t>(draggingTimer)].pitchHz = action.intValue;
                break;
            }
            default:
                return;
            }
            Emit(action);
        };
        for (const auto& hit : hits) {
            if (hit.kind != dragging) continue;
            if (hit.rect.right <= hit.rect.left) return;
            setSlider((hit.kind), (x - hit.rect.left) / (hit.rect.right - hit.rect.left));
            Invalidate();
            return;
        }
    }

    void Click(const Hit& hit, float clickX) {
        UiAction action{};
        action.category = hit.category;
        action.timerIndex = hit.timerIndex;
        switch (hit.kind) {
        case HitKind::Settings:
            OpenSettings();
            return;
        case HitKind::CloseSettings:
            CloseSettings();
            return;
        case HitKind::ModalPanel:
            return;
        case HitKind::CardHeader: {
            auto& category = state.categories[static_cast<std::size_t>(IndexOf(hit.category))];
            category.expanded = !category.expanded;
            if (category.expanded) {
                for (int index = 0; index < 3; ++index) {
                    if (index != IndexOf(hit.category)) state.categories[static_cast<std::size_t>(index)].expanded = false;
                }
            }
            action.kind = ActionKind::SetCategoryExpanded;
            action.enabled = category.expanded;
            Emit(action);
            break;
        }
        case HitKind::CategoryEnabled: {
            auto& category = state.categories[static_cast<std::size_t>(IndexOf(hit.category))];
            category.enabled = !category.enabled;
            action.kind = ActionKind::SetCategoryEnabled;
            action.enabled = category.enabled;
            Emit(action);
            break;
        }
        case HitKind::TimerCount: {
            auto& category = state.categories[static_cast<std::size_t>(IndexOf(hit.category))];
            category.timerCount = std::clamp(hit.value, 1, 3);
            action.kind = ActionKind::SetCategoryTimerCount;
            action.intValue = category.timerCount;
            Emit(action);
            break;
        }
        case HitKind::TimerTts: {
            auto& timer = state.categories[static_cast<std::size_t>(IndexOf(hit.category))].timers[static_cast<std::size_t>(hit.timerIndex)];
            timer.ttsEnabled = !timer.ttsEnabled;
            action.kind = ActionKind::SetTimerTtsEnabled;
            action.enabled = timer.ttsEnabled;
            Emit(action);
            break;
        }
        case HitKind::TimerBeep: {
            auto& timer = state.categories[static_cast<std::size_t>(IndexOf(hit.category))].timers[static_cast<std::size_t>(hit.timerIndex)];
            timer.beepPattern = timer.beepPattern == BeepPattern::Beep ? BeepPattern::Double
                                  : timer.beepPattern == BeepPattern::Double ? BeepPattern::Triple : BeepPattern::Beep;
            action.kind = ActionKind::SetTimerBeepPattern;
            action.beepPattern = timer.beepPattern;
            Emit(action);
            break;
        }
        case HitKind::OverlayEnabled:
            state.overlayEnabled = !state.overlayEnabled;
            action.kind = ActionKind::SetOverlayEnabled;
            action.enabled = state.overlayEnabled;
            Emit(action);
            break;
        case HitKind::OverlayMode:
            state.overlayMode = hit.value == 1 ? OverlayMode::Toast : OverlayMode::Overview;
            action.kind = ActionKind::SetOverlayMode;
            action.mode = state.overlayMode;
            Emit(action);
            break;
        case HitKind::OverlayCategory: {
            const int index = IndexOf(hit.category);
            state.overlayCategories[static_cast<std::size_t>(index)] = !state.overlayCategories[static_cast<std::size_t>(index)];
            action.kind = ActionKind::SetOverlayCategoryEnabled;
            action.enabled = state.overlayCategories[static_cast<std::size_t>(index)];
            Emit(action);
            break;
        }
        case HitKind::PreviewOverlay:
            action.kind = ActionKind::PreviewOverlay;
            Emit(action);
            break;
        case HitKind::BeginOverlayMove:
            action.kind = ActionKind::BeginOverlayMove;
            Emit(action);
            break;
        case HitKind::ResetOverlayPosition:
            action.kind = ActionKind::ResetOverlayPosition;
            Emit(action);
            break;
        case HitKind::ToggleOverlayDebug:
            overlayDebugOpen = !overlayDebugOpen;
            break;
        case HitKind::RefreshOverlayDiagnostics:
            action.kind = ActionKind::RefreshOverlayDiagnostics;
            Emit(action);
            break;
        case HitKind::ClearOverlayDiagnostics:
            action.kind = ActionKind::ClearOverlayDiagnostics;
            Emit(action);
            break;
        case HitKind::SoundEnabled:
            state.soundEnabled = !state.soundEnabled;
            action.kind = ActionKind::SetSoundEnabled;
            action.enabled = state.soundEnabled;
            Emit(action);
            break;
        case HitKind::AutoRefreshEnabled:
            state.autoRefreshEnabled = !state.autoRefreshEnabled;
            action.kind = ActionKind::SetAutoRefreshEnabled;
            action.enabled = state.autoRefreshEnabled;
            Emit(action);
            break;
        case HitKind::SystemToastsEnabled:
            state.systemToastsEnabled = !state.systemToastsEnabled;
            action.kind = ActionKind::SetSystemToastsEnabled;
            action.enabled = state.systemToastsEnabled;
            Emit(action);
            break;
        case HitKind::PanicReset:
            state.panicStop = false;
            action.kind = ActionKind::ResetPanicStop;
            Emit(action);
            break;
        case HitKind::TimerMinutes:
        case HitKind::TimerPitch:
        case HitKind::OverlayScaleX:
        case HitKind::OverlayScaleY:
        case HitKind::OverlayOpacity:
        case HitKind::Volume:
            dragging = hit.kind;
            draggingCategory = hit.category;
            draggingTimer = hit.timerIndex;
            SetCapture(window);
            UpdateSlider(clickX);
            break;
        default:
            break;
        }
        Invalidate();
    }

    void OpenSettings() {
        settingsOpen = true;
        Emit(UiAction{ActionKind::OpenSettings});
        Invalidate();
    }

    void CloseSettings() {
        if (!settingsOpen) return;
        settingsOpen = false;
        if (GetCapture() == window) ReleaseCapture();
        dragging = HitKind::Settings;
        Emit(UiAction{ActionKind::CloseSettings});
        Invalidate();
    }

    bool HandleMessage(UINT message, WPARAM wParam, LPARAM lParam) {
        switch (message) {
        case WM_PAINT: {
            PAINTSTRUCT paint{};
            BeginPaint(window, &paint);
            Render();
            EndPaint(window, &paint);
            return true;
        }
        case WM_ERASEBKGND:
            return true;
        case WM_SIZE:
            if (renderTarget) renderTarget->Resize(D2D1::SizeU(LOWORD(lParam), HIWORD(lParam)));
            InvalidateRect(window, nullptr, FALSE);
            return true;
        case WM_DISPLAYCHANGE:
        case WM_DPICHANGED:
            InvalidateRect(window, nullptr, FALSE);
            return true;
        case WM_KEYDOWN:
            if (wParam == VK_ESCAPE && settingsOpen) {
                CloseSettings();
                return true;
            }
            return false;
        case WM_LBUTTONDOWN: {
            const float x = static_cast<float>(GET_X_LPARAM(lParam));
            const float y = static_cast<float>(GET_Y_LPARAM(lParam));
            Hit* hit = FindHit(x, y);
            if (!hit) {
                if (settingsOpen) CloseSettings();
                return settingsOpen;
            }
            // Last modal hit is backdrop; only close when click really misses panel.
            if (settingsOpen && hit->kind == HitKind::CloseSettings && !Contains(hit->rect, x, y)) {
                CloseSettings();
                return true;
            }
            Click(*hit, x);
            return true;
        }
        case WM_MOUSEMOVE:
            if (dragging != HitKind::Settings && (wParam & MK_LBUTTON) != 0) {
                UpdateSlider(static_cast<float>(GET_X_LPARAM(lParam)));
                return true;
            }
            return false;
        case WM_LBUTTONUP:
            if (dragging != HitKind::Settings) {
                dragging = HitKind::Settings;
                draggingTimer = -1;
                draggingCategory = Category::Helltide;
                if (GetCapture() == window) ReleaseCapture();
                return true;
            }
            return false;
        default:
            return false;
        }
    }
};

MainWindowUi::MainWindowUi(UiCallbacks callbacks) : impl_(new Impl{}) {
    impl_->callbacks = std::move(callbacks);
}

MainWindowUi::~MainWindowUi() {
    delete impl_;
}

HRESULT MainWindowUi::Initialize(HWND window) {
    impl_->window = window;
    return impl_->CreateDeviceIndependentResources();
}

void MainWindowUi::SetState(UiState state) {
    for (auto& category : state.categories) {
        category.timerCount = std::clamp(category.timerCount, 1, 3);
        for (auto& timer : category.timers) {
            timer.minutesBefore = std::clamp(timer.minutesBefore, 0, 60);
            timer.pitchHz = std::clamp(timer.pitchHz, 200, 2000);
        }
    }
    state.overlayScaleX = ClampScale(state.overlayScaleX);
    state.overlayScaleY = ClampScale(state.overlayScaleY);
    state.overlayOpacity = Clamp01(state.overlayOpacity);
    state.volume = Clamp01(state.volume);
    impl_->state = std::move(state);
    InvalidateRect(impl_->window, nullptr, FALSE);
}

const UiState& MainWindowUi::State() const noexcept {
    return impl_->state;
}

void MainWindowUi::OpenSettings() {
    impl_->OpenSettings();
}

void MainWindowUi::CloseSettings() {
    impl_->CloseSettings();
}

bool MainWindowUi::IsSettingsOpen() const noexcept {
    return impl_->settingsOpen;
}

int MainWindowUi::PreferredMainClientHeight(int clientWidth) const noexcept {
    return impl_->PreferredMainClientHeight(clientWidth);
}

void MainWindowUi::Invalidate() const {
    InvalidateRect(impl_->window, nullptr, FALSE);
}

bool MainWindowUi::HandleMessage(UINT message, WPARAM wParam, LPARAM lParam) {
    return impl_->HandleMessage(message, wParam, lParam);
}

void MainWindowUi::Render() {
    impl_->Render();
}

} // namespace helltime::ui
