#pragma once

#include <d2d1.h>
#include <dwrite.h>
#include <windows.h>

#include <array>
#include <cstdint>
#include <functional>
#include <limits>
#include <string>

namespace helltime::ui {

enum class Category : unsigned char {
    Helltide = 0,
    Legion = 1,
    WorldBoss = 2,
};

enum class OverlayMode : unsigned char {
    Overview = 0,
    Toast = 1,
};

enum class BeepPattern : unsigned char {
    Beep = 0,
    Double = 1,
    Triple = 2,
};

enum class ActionKind : unsigned char {
    OpenSettings,
    CloseSettings,
    SetOverlayEnabled,
    SetOverlayMode,
    SetOverlayCategoryEnabled,
    SetOverlayScaleX,
    SetOverlayScaleY,
    SetOverlayOpacity,
    PreviewOverlay,
    BeginOverlayMove,
    ResetOverlayPosition,
    RefreshOverlayDiagnostics,
    ClearOverlayDiagnostics,
    SetVolume,
    SetSoundEnabled,
    SetAutoRefreshEnabled,
    SetSystemToastsEnabled,
    SetCategoryEnabled,
    SetCategoryTimerCount,
    SetCategoryExpanded,
    SetTimerMinutes,
    SetTimerTtsEnabled,
    SetTimerBeepPattern,
    SetTimerPitchHz,
    ResetPanicStop,
};

struct UiAction {
    ActionKind kind{ActionKind::OpenSettings};
    Category category{Category::Helltide};
    int timerIndex{-1};
    bool enabled{false};
    int intValue{0};
    float value{0.0f};
    OverlayMode mode{OverlayMode::Overview};
    BeepPattern beepPattern{BeepPattern::Beep};
};

using ActionSink = std::function<void(const UiAction&)>;

struct TimerView {
    int minutesBefore{30};
    bool ttsEnabled{true};
    BeepPattern beepPattern{BeepPattern::Beep};
    int pitchHz{880};
};

struct CategoryView {
    std::wstring title;
    std::wstring subtitle;
    std::wstring countdown{L"—"};
    std::wstring eventTime;
    // Source ordering uses the visible event target, not the fixed category order.
    std::int64_t targetMs{std::numeric_limits<std::int64_t>::max()};
    bool enabled{true};
    bool active{false};
    bool expanded{false};
    int timerCount{3};
    std::array<TimerView, 3> timers{};
};

struct OverlayDiagnosticsView {
    std::uintptr_t hwnd{0};
    bool exists{false};
    bool visible{false};
    bool renderSucceeded{false};
    bool showSucceeded{false};
    bool updateLayeredWindowSucceeded{false};
    bool positioning{false};
    OverlayMode mode{OverlayMode::Overview};
    RECT bounds{};
    HRESULT lastHresult{S_OK};
    DWORD lastWin32Error{ERROR_SUCCESS};
    std::wstring lastError{};
    ULONGLONG lastSuccessfulFrameTick{0};
    std::uint64_t nonZeroAlphaPixels{0};
    std::uint64_t nonZeroColorPixels{0};
    std::uint8_t maxAlpha{0};
    bool frameHadAlpha{false};
    bool gatePanicStop{false};
    bool gateOverlayEnabled{false};
    bool gateSettingsEnabled{false};
    bool gateOverviewRows{false};
    bool gateToastMode{false};
    bool gateReminderActive{false};
    std::array<std::wstring, 8> recentEvents{};
    std::size_t recentEventCount{0};
};

struct UiState {
    std::array<CategoryView, 3> categories{};
    bool panicStop{false};
    bool overlayEnabled{true};
    OverlayMode overlayMode{OverlayMode::Overview};
    std::array<bool, 3> overlayCategories{true, true, true};
    float overlayScaleX{1.0f};
    float overlayScaleY{1.0f};
    float overlayOpacity{0.2f};
    float volume{0.8f};
    bool soundEnabled{true};
    bool autoRefreshEnabled{false};
    bool systemToastsEnabled{false};
    OverlayDiagnosticsView overlayDiagnostics{};
};

struct UiCallbacks {
    ActionSink onAction;
};

// Owns only main-window painting and input. Domain/settings code supplies UiState
// and receives generic UiAction values through UiCallbacks::onAction.
class MainWindowUi final {
public:
    explicit MainWindowUi(UiCallbacks callbacks = {});
    ~MainWindowUi();

    MainWindowUi(const MainWindowUi&) = delete;
    MainWindowUi& operator=(const MainWindowUi&) = delete;

    HRESULT Initialize(HWND window);
    void SetState(UiState state);
    const UiState& State() const noexcept;

    void OpenSettings();
    void CloseSettings();
    bool IsSettingsOpen() const noexcept;
    int PreferredMainClientHeight(int clientWidth) const noexcept;
    void Invalidate() const;

    // Main window forwards messages here. Returns true when UI consumed message.
    bool HandleMessage(UINT message, WPARAM wParam, LPARAM lParam);
    void Render();

private:
    struct Impl;
    Impl* impl_;
};

} // namespace helltime::ui
