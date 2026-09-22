#pragma once

#include "../domain/schedule.h"
#include "../domain/settings.h"
#include "../ui/main_window.h"

#include <windows.h>

namespace helltime::integration {

class OverlayWindow final {
public:
    OverlayWindow() = default;
    ~OverlayWindow();
    OverlayWindow(const OverlayWindow&) = delete;
    OverlayWindow& operator=(const OverlayWindow&) = delete;

    bool Create(HINSTANCE instance);
    void Update(const ui::UiState& state, const domain::Settings& settings,
                const domain::Schedule& schedule, std::int64_t nowMs);
    void ShowReminderToast(const std::wstring& title, const std::wstring& body);
    void BeginMove();
    void Hide();
    void Destroy();

private:
    static LRESULT CALLBACK WindowProc(HWND, UINT, WPARAM, LPARAM);
    LRESULT HandleMessage(UINT, WPARAM, LPARAM);
    void Render(const ui::UiState&, const domain::Settings&, const domain::Schedule&, std::int64_t);
    void SetClickThrough(bool enabled);
    void ClampPosition();
    void LoadPosition();
    void SavePosition() const;

    HINSTANCE instance_{nullptr};
    HWND window_{nullptr};
    POINT position_{40, 40};
    POINT dragOffset_{};
    bool dragging_{false};
    bool positioning_{false};
    ULONGLONG positioningUntil_{0};
    std::wstring reminderTitle_{};
    std::wstring reminderBody_{};
    ULONGLONG reminderUntil_{0};
    int width_{390};
    int height_{150};
};

} // namespace helltime::integration
