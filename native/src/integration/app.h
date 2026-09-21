#pragma once

#include "audio.h"
#include "overlay.h"

#include "../domain/schedule.h"
#include "../domain/settings.h"
#include "../ui/main_window.h"

#include <windows.h>
#include <shellapi.h>

#include <optional>

namespace helltime::integration {

class NativeApp final {
public:
    NativeApp(HINSTANCE instance, int showCommand);
    ~NativeApp();
    int Run();

private:
    static LRESULT CALLBACK WindowProc(HWND, UINT, WPARAM, LPARAM);
    LRESULT HandleMessage(UINT, WPARAM, LPARAM);
    void Tick();
    void Refresh(bool preserveUiState);
    void ApplyAction(const ui::UiAction&);
    ui::UiState MakeUiState(std::int64_t nowMs) const;
    void CreateTray();
    void RemoveTray();
    void ShowTrayMenu();
    void ShowReminderToast(const wchar_t* title, const wchar_t* text);
    void SetVisible(bool visible);

    HINSTANCE instance_{nullptr};
    HWND window_{nullptr};
    int showCommand_{SW_SHOWNORMAL};
    UINT_PTR timer_{0};
    NOTIFYICONDATAW tray_{};
    domain::Settings settings_{};
    domain::Schedule schedule_{};
    std::optional<std::int64_t> previousNowMs_{};
    ui::MainWindowUi ui_;
    OverlayWindow overlay_;
};

} // namespace helltime::integration
