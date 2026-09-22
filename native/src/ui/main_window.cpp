#include "main_window.h"

#include <algorithm>
#include <array>
#include <cmath>
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

constexpr D2D1_COLOR_F kBackground{0.043f, 0.043f, 0.043f, 1.0f};
constexpr D2D1_COLOR_F kBackgroundTop{0.075f, 0.025f, 0.025f, 0.30f};
constexpr D2D1_COLOR_F kPanel{0.04f, 0.04f, 0.04f, 0.96f};
constexpr D2D1_COLOR_F kPanelRaised{0.08f, 0.08f, 0.08f, 0.98f};
constexpr D2D1_COLOR_F kGold{0.831f, 0.686f, 0.216f, 1.0f};
constexpr D2D1_COLOR_F kGoldDim{0.627f, 0.518f, 0.157f, 1.0f};
constexpr D2D1_COLOR_F kText{1.0f, 1.0f, 1.0f, 0.96f};
constexpr D2D1_COLOR_F kTextSecondary{1.0f, 1.0f, 1.0f, 0.78f};
constexpr D2D1_COLOR_F kMuted{1.0f, 1.0f, 1.0f, 0.56f};
constexpr D2D1_COLOR_F kBorder{0.47f, 0.47f, 0.47f, 0.36f};
constexpr D2D1_COLOR_F kHelltide{0.60f, 0.122f, 0.122f, 1.0f};
constexpr D2D1_COLOR_F kLegion{0.702f, 0.141f, 0.141f, 1.0f};
constexpr D2D1_COLOR_F kWorldBoss{0.478f, 0.086f, 0.086f, 1.0f};

int IndexOf(Category category) noexcept {
    return static_cast<int>(category);
}

Category CategoryAt(int index) noexcept {
    return static_cast<Category>(std::clamp(index, 0, 2));
}

D2D1_COLOR_F CategoryColor(Category category, float alpha = 1.0f) noexcept {
    D2D1_COLOR_F color = kHelltide;
    if (category == Category::Legion) color = kLegion;
    if (category == Category::WorldBoss) color = kWorldBoss;
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

    HRESULT CreateTextFormat(float size, DWRITE_FONT_WEIGHT weight, IDWriteTextFormat** result) {
        HRESULT hr = writeFactory->CreateTextFormat(
            L"Segoe UI", nullptr, weight, DWRITE_FONT_STYLE_NORMAL,
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
        hr = CreateTextFormat(18.0f, DWRITE_FONT_WEIGHT_EXTRA_BOLD, &titleFormat);
        if (FAILED(hr)) return hr;
        hr = CreateTextFormat(13.0f, DWRITE_FONT_WEIGHT_EXTRA_BOLD, &headingFormat);
        if (FAILED(hr)) return hr;
        hr = CreateTextFormat(12.0f, DWRITE_FONT_WEIGHT_SEMI_BOLD, &bodyFormat);
        if (FAILED(hr)) return hr;
        hr = CreateTextFormat(10.0f, DWRITE_FONT_WEIGHT_NORMAL, &smallFormat);
        if (FAILED(hr)) return hr;
        hr = CreateTextFormat(20.0f, DWRITE_FONT_WEIGHT_BLACK, &countdownFormat);
        if (FAILED(hr)) return hr;
        return CreateTextFormat(11.0f, DWRITE_FONT_WEIGHT_BOLD, &buttonFormat);
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
        hr = renderTarget->CreateSolidColorBrush(kPanel, &brush);
        if (SUCCEEDED(hr)) hr = renderTarget->CreateSolidColorBrush(kText, &textBrush);
        if (SUCCEEDED(hr)) hr = renderTarget->CreateSolidColorBrush(kTextSecondary, &secondaryBrush);
        if (SUCCEEDED(hr)) hr = renderTarget->CreateSolidColorBrush(kMuted, &mutedBrush);
        if (SUCCEEDED(hr)) hr = renderTarget->CreateSolidColorBrush(kBorder, &borderBrush);
        if (SUCCEEDED(hr)) hr = renderTarget->CreateSolidColorBrush(kGold, &goldBrush);
        if (SUCCEEDED(hr)) hr = renderTarget->CreateSolidColorBrush(kGold, &redBrush);
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

    void StrokeRounded(const D2D1_RECT_F& rect, float radius, D2D1_COLOR_F color, float width = 1.0f) {
        SetBrush(borderBrush, color);
        renderTarget->DrawRoundedRectangle(D2D1::RoundedRect(rect, radius, radius), borderBrush, width);
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
        SetBrush(goldBrush, enabled ? kGold : kGoldDim);
        renderTarget->DrawLine(D2D1::Point2F(left, center), D2D1::Point2F(knob, center), goldBrush, 3.0f);
        renderTarget->FillEllipse(D2D1::Ellipse(D2D1::Point2F(knob, center), 5.0f, 5.0f), goldBrush);
    }

    void DrawToggle(D2D1_RECT_F rect, bool checked, bool enabled = true) {
        const auto fill = checked ? (enabled ? kGold : kGoldDim)
                                  : D2D1::ColorF(0.15f, 0.15f, 0.15f, enabled ? 1.0f : 0.65f);
        FillRounded(rect, 7.0f, fill);
        StrokeRounded(rect, 7.0f, enabled ? kBorder : D2D1::ColorF(0.3f, 0.3f, 0.3f, 0.25f));
        const float x = checked ? rect.right - 8.0f : rect.left + 8.0f;
        SetBrush(secondaryBrush, checked ? D2D1::ColorF(0.1f, 0.07f, 0.02f, 1.0f)
                                         : D2D1::ColorF(0.70f, 0.70f, 0.70f, enabled ? 1.0f : 0.65f));
        renderTarget->FillEllipse(D2D1::Ellipse(D2D1::Point2F(x, (rect.top + rect.bottom) * 0.5f), 5.0f, 5.0f), secondaryBrush);
    }

    void DrawCheckbox(float x, float y, bool checked, bool enabled = true) {
        const D2D1_RECT_F box = D2D1::RectF(x, y, x + 14.0f, y + 14.0f);
        FillRounded(box, 3.0f, checked ? (enabled ? kGold : kGoldDim)
                                        : D2D1::ColorF(0.08f, 0.08f, 0.08f, enabled ? 1.0f : 0.6f));
        StrokeRounded(box, 3.0f, enabled ? kBorder : D2D1::ColorF(0.3f, 0.3f, 0.3f, 0.25f));
        if (checked) {
            Text(L"+", buttonFormat, secondaryBrush, box, DWRITE_TEXT_ALIGNMENT_CENTER);
        }
    }

    void DrawButton(const D2D1_RECT_F& rect, std::wstring_view label, bool primary = false) {
        FillRounded(rect, 5.0f, primary ? D2D1::ColorF(kGold.r, kGold.g, kGold.b, 0.24f)
                                       : D2D1::ColorF(0.10f, 0.10f, 0.10f, 0.96f));
        StrokeRounded(rect, 5.0f, primary ? kGold : kBorder);
        Text(label, buttonFormat, primary ? textBrush : secondaryBrush, rect, DWRITE_TEXT_ALIGNMENT_CENTER);
    }

    void DrawCategoryCard(Category category, const CategoryView& categoryView,
                          const D2D1_RECT_F& card, bool compact) {
        const D2D1_COLOR_F accent = CategoryColor(category);
        const std::array<const wchar_t*, 3> defaults{L"Helltide", L"Legion", L"World Boss"};
        const std::wstring_view title = categoryView.title.empty() ? defaults[static_cast<std::size_t>(IndexOf(category))]
                                                                    : std::wstring_view(categoryView.title);
        FillRounded(card, 8.0f, categoryView.enabled ? kPanel : D2D1::ColorF(0.03f, 0.03f, 0.03f, 0.86f));
        StrokeRounded(card, 8.0f, D2D1::ColorF(accent.r, accent.g, accent.b, categoryView.enabled ? 0.42f : 0.18f));
        FillRounded(D2D1::RectF(card.left, card.top, card.left + 4.0f, card.bottom), 2.0f,
                    D2D1::ColorF(accent.r, accent.g, accent.b, categoryView.enabled ? 0.94f : 0.38f));

        const D2D1_RECT_F header = D2D1::RectF(card.left + 14.0f, card.top + 7.0f, card.right - 12.0f,
                                               card.top + (compact ? 53.0f : 58.0f));
        AddHit(header, HitKind::CardHeader, category);
        Text(title, headingFormat,
             categoryView.enabled ? textBrush : mutedBrush,
             D2D1::RectF(header.left, header.top, header.right - 94.0f, header.top + 22.0f));
        // TTS names are configuration, not card content. The Tauri view keeps cards event-focused too.

        Text(categoryView.countdown.empty() ? L"—" : categoryView.countdown, countdownFormat,
             categoryView.enabled ? goldBrush : mutedBrush,
             D2D1::RectF(header.right - 88.0f, header.top + 2.0f, header.right, header.top + 30.0f),
             DWRITE_TEXT_ALIGNMENT_TRAILING);
        Text(categoryView.eventTime.empty() ? (categoryView.active ? L"läuft" : L"nächster Termin")
                                            : categoryView.eventTime,
             smallFormat, mutedBrush,
             D2D1::RectF(header.right - 110.0f, header.top + 32.0f, header.right, header.bottom),
             DWRITE_TEXT_ALIGNMENT_TRAILING);

        const D2D1_RECT_F toggle = D2D1::RectF(card.right - 42.0f, card.top + 9.0f, card.right - 16.0f, card.top + 23.0f);
        DrawToggle(toggle, categoryView.enabled);
        AddHit(D2D1::RectF(card.right - 50.0f, card.top + 3.0f, card.right - 8.0f, card.top + 31.0f),
               HitKind::CategoryEnabled, category);

        if (!categoryView.expanded) return;
        const float bodyTop = card.top + (compact ? 62.0f : 66.0f);
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
        const float margin = 8.0f;
        const float gap = 6.0f;
        const float contentWidth = std::min(600.0f, std::max(200.0f, width - margin * 2.0f));
        const float contentLeft = (width - contentWidth) * 0.5f;
        const float headerBottom = 62.0f;
        const float cardWidth = contentWidth;
        const float cardBaseHeight = 98.0f;
        float y = headerBottom;
        for (int i = 0; i < 3; ++i) {
            const auto& category = state.categories[static_cast<std::size_t>(i)];
            const float cardHeight = category.expanded ? 278.0f : cardBaseHeight;
            DrawCategoryCard(CategoryAt(i), category,
                             D2D1::RectF(contentLeft, y, contentLeft + cardWidth, y + cardHeight), true);
            y += cardHeight + gap;
        }
        const float footerTop = y + 2.0f;
        const float footerBottom = std::min(height - 8.0f, footerTop + 36.0f);
        if (footerBottom > footerTop) {
            FillRounded(D2D1::RectF(contentLeft, footerTop, contentLeft + contentWidth, footerBottom), 7.0f,
                        D2D1::ColorF(0.08f, 0.08f, 0.08f, 0.94f));
            StrokeRounded(D2D1::RectF(contentLeft, footerTop, contentLeft + contentWidth, footerBottom), 7.0f, kBorder);
            Text(L"Overlay", smallFormat, mutedBrush,
                 D2D1::RectF(contentLeft + 12.0f, footerTop, contentLeft + 62.0f, footerBottom));
            const D2D1_RECT_F overlayToggle = D2D1::RectF(contentLeft + 68.0f, footerTop + 11.0f, contentLeft + 94.0f, footerTop + 25.0f);
            DrawToggle(overlayToggle, state.overlayEnabled);
            AddHit(D2D1::RectF(contentLeft + 62.0f, footerTop + 4.0f, contentLeft + 101.0f, footerBottom - 4.0f), HitKind::OverlayEnabled);
            const D2D1_RECT_F settings = D2D1::RectF(contentLeft + contentWidth - 90.0f, footerTop + 5.0f,
                                                     contentLeft + contentWidth - 10.0f, footerBottom - 5.0f);
            DrawButton(settings, L"Einstellungen", true);
            AddHit(settings, HitKind::Settings);
        }
        AddHit(D2D1::RectF(width - margin - 52.0f, 8.0f, width - margin, 50.0f), HitKind::Settings);
    }

    void DrawHeader(float width) {
        FillRect(D2D1::RectF(0.0f, 0.0f, width, 3.0f), kGold);
        Text(L"hell", titleFormat, textBrush, D2D1::RectF(14.0f, 8.0f, 54.0f, 34.0f));
        SetBrush(redBrush, kLegion);
        Text(L"time", titleFormat, redBrush, D2D1::RectF(51.0f, 8.0f, 94.0f, 34.0f));
        Text(L"Event Timers", smallFormat, mutedBrush, D2D1::RectF(15.0f, 35.0f, 130.0f, 54.0f));
        const D2D1_RECT_F settings = D2D1::RectF(width - 60.0f, 10.0f, width - 14.0f, 42.0f);
        DrawButton(settings, L"\u2699");
        AddHit(settings, HitKind::Settings);
    }

    void DrawModalSection(const D2D1_RECT_F& section) {
        FillRounded(section, 7.0f, D2D1::ColorF(0.04f, 0.04f, 0.04f, 0.96f));
        StrokeRounded(section, 7.0f, kBorder);
    }

    void DrawModal(float width, float height) {
        FillRect(D2D1::RectF(0.0f, 0.0f, width, height), D2D1::ColorF(0.0f, 0.0f, 0.0f, 0.65f));
        const float modalWidth = std::min(680.0f, std::max(280.0f, width - 16.0f));
        const float left = (width - modalWidth) * 0.5f;
        const float top = 10.0f;
        const float bottom = std::max(top + 260.0f, std::min(height - 10.0f, top + 640.0f));
        const D2D1_RECT_F modal = D2D1::RectF(left, top, left + modalWidth, bottom);
        FillRounded(modal, 10.0f, D2D1::ColorF(0.015f, 0.015f, 0.015f, 0.99f));
        StrokeRounded(modal, 10.0f, kGold);
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
        DrawButton(preview, L"Vorschau", true);
        DrawButton(position, L"Position", true);
        DrawButton(resetPosition, L"Reset Position");
        AddHit(preview, HitKind::PreviewOverlay);
        AddHit(position, HitKind::BeginOverlayMove);
        AddHit(resetPosition, HitKind::ResetOverlayPosition);
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

        // Panel hit is registered before controls, so controls win reverse hit-test order.
    }

    void Render() {
        if (!window || FAILED(CreateDeviceResources())) return;
        RECT client{};
        GetClientRect(window, &client);
        const float width = static_cast<float>(std::max<LONG>(1, client.right - client.left));
        const float height = static_cast<float>(std::max<LONG>(1, client.bottom - client.top));
        renderTarget->BeginDraw();
        renderTarget->Clear(kBackground);
        FillRect(D2D1::RectF(0.0f, 0.0f, width, std::min(height, 170.0f)), kBackgroundTop);
        hits.clear();
        if (settingsOpen) {
            DrawModal(width, height);
        } else {
            DrawHeader(width);
            BuildMainLayout(width, height);
            if (state.panicStop) {
                const D2D1_RECT_F warning = D2D1::RectF(12.0f, std::min(height - 38.0f, 60.0f), width - 72.0f, std::min(height - 10.0f, 86.0f));
                FillRounded(warning, 6.0f, D2D1::ColorF(0.20f, 0.08f, 0.03f, 0.94f));
                StrokeRounded(warning, 6.0f, kGoldDim);
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
