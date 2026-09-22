#pragma once

#include "../domain/schedule.h"
#include "../domain/settings.h"
#include "../ui/main_window.h"

#include <windows.h>

#include <array>
#include <cstdint>
#include <string>

namespace helltime::integration {

struct OverlayDiagnostics {
    HWND hwnd{nullptr};
    bool created{false};
    bool renderSucceeded{false};
    bool showSucceeded{false};
    bool visible{false};
    bool positioning{false};
    ui::OverlayMode mode{ui::OverlayMode::Overview};
    RECT bounds{};
    DWORD lastWin32Error{ERROR_SUCCESS};
    HRESULT lastHresult{S_OK};
    std::wstring lastError{};
    ULONGLONG lastSuccessfulFrameTick{0};
    std::uint64_t lastNonZeroAlphaPixels{0};
    std::uint64_t lastNonZeroColorPixels{0};
    std::uint8_t lastMaxAlpha{0};
    bool lastFrameHadAlpha{false};
    std::array<std::wstring, 8> recentEvents{};
    std::size_t recentEventCount{0};
};

class OverlayWindow final {
public:
    OverlayWindow() = default;
    ~OverlayWindow();
    OverlayWindow(const OverlayWindow&) = delete;
    OverlayWindow& operator=(const OverlayWindow&) = delete;

    bool Create(HINSTANCE instance);
    void Update(const ui::UiState& state, const domain::Settings& settings,
                const domain::Schedule& schedule, std::int64_t nowMs);
    void ShowToast(const std::wstring& title, const std::wstring& body,
                   const std::wstring& category = {}, ULONGLONG durationMs = 5200);
    void ShowReminderToast(const std::wstring& title, const std::wstring& body);
    void ShowPreviewToast(const std::wstring& title, const std::wstring& body);
    void BeginMove();
    void ResetPosition();
    void Hide();
    void Destroy();
    OverlayDiagnostics GetDiagnostics() const;

private:
    static LRESULT CALLBACK WindowProc(HWND, UINT, WPARAM, LPARAM);
    LRESULT HandleMessage(UINT, WPARAM, LPARAM);
    bool Render(const ui::UiState&, const domain::Settings&, const domain::Schedule&, std::int64_t);
    void SetClickThrough(bool enabled);
    void ClampPosition();
    void LoadPosition();
    void SavePosition() const;
    void RecordEvent(const wchar_t* event);
    void RecordError(const wchar_t* step, HRESULT result = S_OK, DWORD win32Error = ERROR_SUCCESS);
    void RefreshDiagnosticsBounds();

    HINSTANCE instance_{nullptr};
    HWND window_{nullptr};
    POINT position_{40, 40};
    POINT dragOffset_{};
    bool dragging_{false};
    bool positioning_{false};
    ULONGLONG positioningUntil_{0};
    std::wstring reminderTitle_{};
    std::wstring reminderBody_{};
    std::wstring reminderCategory_{};
    ULONGLONG reminderUntil_{0};
    int width_{304};
    int height_{132};
    OverlayDiagnostics diagnostics_{};
};

} // namespace helltime::integration
