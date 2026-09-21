#include "app.h"

#include "../domain/safety.h"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <shellapi.h>

#include <algorithm>
#include <array>
#include <ctime>
#include <iomanip>
#include <sstream>
#include <string>

namespace helltime::integration {
namespace {

constexpr wchar_t kClassName[] = L"HelltimeNativeMainWindow";
constexpr wchar_t kTitle[] = L"Helltime";
constexpr UINT kTrayMessage = WM_APP + 1;
constexpr UINT kTimerId = 1;
constexpr UINT kTrayShow = 1001;
constexpr UINT kTraySettings = 1002;
constexpr UINT kTrayMoveOverlay = 1003;
constexpr UINT kTrayOpenUrl = 1004;
constexpr UINT kTrayResetPanic = 1005;
constexpr UINT kTrayExit = 1006;

std::wstring countdown(std::int64_t milliseconds) {
    if (milliseconds <= 0) return L"READY";
    auto seconds = milliseconds / 1000;
    const auto hours = seconds / 3600;
    seconds %= 3600;
    const auto minutes = seconds / 60;
    seconds %= 60;
    std::wostringstream result;
    if (hours > 0) result << hours << L":" << std::setfill(L'0') << std::setw(2);
    result << minutes << L":" << std::setfill(L'0') << std::setw(2) << seconds;
    return result.str();
}

std::wstring localTime(std::int64_t milliseconds) {
    const auto seconds = static_cast<std::time_t>(milliseconds / 1000);
    std::tm time{};
    localtime_s(&time, &seconds);
    std::wostringstream result;
    result << std::setfill(L'0') << std::setw(2) << time.tm_hour << L":" << std::setw(2) << time.tm_min;
    return result.str();
}

std::wstring utf8Wide(const std::string& text) {
    if (text.empty()) return {};
    const int size = MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), nullptr, 0);
    if (size <= 0) return std::wstring(text.begin(), text.end());
    std::wstring result(static_cast<std::size_t>(size), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), result.data(), size);
    return result;
}

const std::vector<domain::ScheduleItem>& itemsFor(const domain::Schedule& schedule, int index) {
    if (index == 0) return schedule.helltide;
    if (index == 1) return schedule.legion;
    return schedule.worldBoss;
}

} // namespace

NativeApp::NativeApp(HINSTANCE instance, int showCommand)
    : instance_(instance), showCommand_(showCommand), ui_({[this](const ui::UiAction& action) { ApplyAction(action); }}) {}

NativeApp::~NativeApp() {
    if (timer_) KillTimer(window_, timer_);
    RemoveTray();
    overlay_.Destroy();
}

int NativeApp::Run() {
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    WNDCLASSEXW klass{sizeof(klass)};
    klass.hInstance = instance_;
    klass.lpfnWndProc = WindowProc;
    klass.lpszClassName = kClassName;
    klass.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    klass.hbrBackground = nullptr;
    klass.style = CS_HREDRAW | CS_VREDRAW;
    RegisterClassExW(&klass);
    window_ = CreateWindowExW(0, kClassName, kTitle, WS_OVERLAPPEDWINDOW,
                              CW_USEDEFAULT, CW_USEDEFAULT, 980, 660, nullptr, nullptr, instance_, this);
    if (!window_) { CoUninitialize(); return 1; }
    if (FAILED(ui_.Initialize(window_))) { DestroyWindow(window_); CoUninitialize(); return 1; }
    settings_ = domain::loadSettings();
    overlay_.Create(instance_);
    CreateTray();
    timer_ = SetTimer(window_, kTimerId, 1000, nullptr);
    Refresh(false);
    ShowWindow(window_, showCommand_ == SW_HIDE ? SW_SHOWNORMAL : showCommand_);
    UpdateWindow(window_);
    MSG message{};
    while (GetMessageW(&message, nullptr, 0, 0) > 0) {
        TranslateMessage(&message);
        DispatchMessageW(&message);
    }
    UnregisterClassW(kClassName, instance_);
    CoUninitialize();
    return static_cast<int>(message.wParam);
}

void NativeApp::SetVisible(bool visible) {
    if (!window_) return;
    ShowWindow(window_, visible ? SW_SHOWNORMAL : SW_HIDE);
    if (visible) { SetForegroundWindow(window_); UpdateWindow(window_); }
}

ui::UiState NativeApp::MakeUiState(std::int64_t nowMs) const {
    ui::UiState result{};
    result.panicStop = domain::isPanicStopEnabled();
    result.overlayEnabled = settings_.overlayWindowEnabled;
    result.overlayMode = settings_.overlayWindowMode == domain::OverlayWindowMode::Toast ? ui::OverlayMode::Toast : ui::OverlayMode::Overview;
    result.overlayCategories = settings_.overlayWindowCategories;
    result.overlayScaleX = static_cast<float>(settings_.overlayScaleX);
    result.overlayScaleY = static_cast<float>(settings_.overlayScaleY);
    result.overlayOpacity = static_cast<float>(settings_.overlayBgOpacity);
    result.volume = static_cast<float>(settings_.volume);
    result.soundEnabled = settings_.soundEnabled;
    result.autoRefreshEnabled = settings_.autoRefreshEnabled;
    result.systemToastsEnabled = settings_.systemToastsEnabled;
    const std::array<const wchar_t*, 3> titles{L"Helltide", L"Legion", L"World Boss"};
    for (int index = 0; index < 3; ++index) {
        auto& category = result.categories[static_cast<std::size_t>(index)];
        const auto& configured = settings_.categories[static_cast<std::size_t>(index)];
        category.title = titles[static_cast<std::size_t>(index)];
        category.subtitle = utf8Wide(configured.ttsName);
        category.enabled = configured.enabled;
        category.timerCount = configured.timerCount;
        for (int timer = 0; timer < 3; ++timer) {
            const auto& source = configured.timers[static_cast<std::size_t>(timer)];
            auto& destination = category.timers[static_cast<std::size_t>(timer)];
            destination.minutesBefore = source.minutesBefore;
            destination.ttsEnabled = source.ttsEnabled;
            destination.beepPattern = static_cast<ui::BeepPattern>(source.beepPattern);
            destination.pitchHz = source.pitchHz;
        }
        const auto& items = itemsFor(schedule_, index);
        if (index == 0) {
            const auto timing = domain::findActiveOrNextHelltide(items, nowMs);
            if (timing) {
                category.active = timing->active;
                category.countdown = countdown(timing->targetMs - nowMs);
                category.eventTime = timing->active ? L"bis " + localTime(timing->targetMs) : L"ab " + localTime(timing->startMs);
            }
        } else {
            const auto next = domain::findNext(items, nowMs);
            if (next) {
                category.countdown = countdown(next->timestamp * 1000 - nowMs);
                category.eventTime = localTime(next->timestamp * 1000);
            }
        }
    }
    return result;
}

void NativeApp::Refresh(bool preserveUiState) {
    const auto now = domain::nowMilliseconds();
    const auto old = ui_.State();
    schedule_ = domain::generateSchedule(now);
    auto next = MakeUiState(now);
    if (preserveUiState) {
        for (int i = 0; i < 3; ++i) next.categories[static_cast<std::size_t>(i)].expanded = old.categories[static_cast<std::size_t>(i)].expanded;
    }
    ui_.SetState(next);
    overlay_.Update(next, settings_, schedule_, now);
    InvalidateRect(window_, nullptr, FALSE);
}

void NativeApp::Tick() {
    const auto now = domain::nowMilliseconds();
    if (domain::isPanicStopEnabled() && !ui_.State().panicStop) settings_ = domain::loadSettings();
    schedule_ = domain::generateSchedule(now);
    if (previousNowMs_ && !domain::isPanicStopEnabled()) {
        for (int category = 0; category < 3; ++category) {
            const auto& configured = settings_.categories[static_cast<std::size_t>(category)];
            if (!configured.enabled) continue;
            const auto& items = itemsFor(schedule_, category);
            std::array<int, 3> minutes{};
            for (int timer = 0; timer < 3; ++timer) minutes[static_cast<std::size_t>(timer)] = configured.timers[static_cast<std::size_t>(timer)].minutesBefore;
            for (const auto& item : items) {
                const auto due = domain::findDueReminderTimers(item, minutes, configured.timerCount, now, previousNowMs_);
                if (due.due.empty()) continue;
                const auto announce = [&](const domain::DueReminderTimer& reminder) {
                    const auto& timer = configured.timers[static_cast<std::size_t>(reminder.index)];
                    const auto name = configured.ttsName.empty() ? std::string("Helltime") : configured.ttsName;
                    if (settings_.soundEnabled && timer.ttsEnabled) {
                        SpeakReminder(name + " in " + std::to_string(timer.minutesBefore) + " Minuten", static_cast<int>(settings_.volume * 100.0));
                    }
                    const auto toastText = utf8Wide(name + " in " + std::to_string(timer.minutesBefore) + " Minuten");
                    if (!toastText.empty()) {
                        const auto& categoryView = ui_.State().categories[static_cast<std::size_t>(category)];
                        overlay_.ShowReminderToast(categoryView.title, toastText);
                        ShowReminderToast(L"Helltime Erinnerung", toastText.c_str());
                    }
                    if (settings_.soundEnabled) PlayReminderBeep(timer.beepPattern, timer.pitchHz, settings_.volume);
                };
                if (due.catchUp) {
                    const auto latest = std::max_element(due.due.begin(), due.due.end(),
                        [](const auto& left, const auto& right) { return left.triggerMs < right.triggerMs; });
                    announce(*latest);
                } else {
                    for (const auto& reminder : due.due) announce(reminder);
                }
            }
        }
    }
    previousNowMs_ = now;
    Refresh(true);
}

void NativeApp::ApplyAction(const ui::UiAction& action) {
    const auto index = static_cast<std::size_t>(static_cast<int>(action.category));
    switch (action.kind) {
    case ui::ActionKind::SetOverlayEnabled: settings_.overlayWindowEnabled = action.enabled; break;
    case ui::ActionKind::SetOverlayMode: settings_.overlayWindowMode = action.mode == ui::OverlayMode::Toast ? domain::OverlayWindowMode::Toast : domain::OverlayWindowMode::Overview; break;
    case ui::ActionKind::SetOverlayCategoryEnabled: settings_.overlayWindowCategories[index] = action.enabled; break;
    case ui::ActionKind::SetOverlayScaleX: settings_.overlayScaleX = action.value; break;
    case ui::ActionKind::SetOverlayScaleY: settings_.overlayScaleY = action.value; break;
    case ui::ActionKind::SetOverlayOpacity: settings_.overlayBgOpacity = action.value; break;
    case ui::ActionKind::SetVolume: settings_.volume = action.value; break;
    case ui::ActionKind::SetSoundEnabled: settings_.soundEnabled = action.enabled; break;
    case ui::ActionKind::SetAutoRefreshEnabled: settings_.autoRefreshEnabled = action.enabled; break;
    case ui::ActionKind::SetSystemToastsEnabled: settings_.systemToastsEnabled = action.enabled; break;
    case ui::ActionKind::SetCategoryEnabled: settings_.categories[index].enabled = action.enabled; break;
    case ui::ActionKind::SetCategoryTimerCount: settings_.categories[index].timerCount = action.intValue; break;
    case ui::ActionKind::SetTimerMinutes: settings_.categories[index].timers[static_cast<std::size_t>(action.timerIndex)].minutesBefore = action.intValue; break;
    case ui::ActionKind::SetTimerTtsEnabled: settings_.categories[index].timers[static_cast<std::size_t>(action.timerIndex)].ttsEnabled = action.enabled; break;
    case ui::ActionKind::SetTimerBeepPattern: settings_.categories[index].timers[static_cast<std::size_t>(action.timerIndex)].beepPattern = static_cast<domain::BeepPattern>(action.beepPattern); break;
    case ui::ActionKind::SetTimerPitchHz: settings_.categories[index].timers[static_cast<std::size_t>(action.timerIndex)].pitchHz = action.intValue; break;
    case ui::ActionKind::ResetPanicStop:
        domain::disablePanicStop(); settings_ = domain::loadSettings(); previousNowMs_.reset(); Refresh(true); return;
    default: return;
    }
    settings_ = domain::normalizeSettings(settings_);
    domain::saveSettings(settings_);
    Refresh(true);
}

void NativeApp::CreateTray() {
    tray_.cbSize = sizeof(tray_); tray_.hWnd = window_; tray_.uID = 1;
    tray_.uFlags = NIF_MESSAGE | NIF_ICON | NIF_TIP; tray_.uCallbackMessage = kTrayMessage;
    tray_.hIcon = LoadIconW(nullptr, IDI_APPLICATION); wcscpy_s(tray_.szTip, L"Helltime");
    Shell_NotifyIconW(NIM_ADD, &tray_);
}

void NativeApp::RemoveTray() { if (tray_.hWnd) Shell_NotifyIconW(NIM_DELETE, &tray_); tray_.hWnd = nullptr; }

void NativeApp::ShowReminderToast(const wchar_t* title, const wchar_t* text) {
    if (!settings_.systemToastsEnabled) return;
    tray_.uFlags = NIF_INFO;
    wcscpy_s(tray_.szInfoTitle, title);
    wcscpy_s(tray_.szInfo, text);
    tray_.dwInfoFlags = NIIF_INFO;
    Shell_NotifyIconW(NIM_MODIFY, &tray_);
}

void NativeApp::ShowTrayMenu() {
    POINT point{}; GetCursorPos(&point);
    HMENU menu = CreatePopupMenu();
    AppendMenuW(menu, MF_STRING, kTrayShow, IsWindowVisible(window_) ? L"Fenster verstecken" : L"Fenster anzeigen");
    AppendMenuW(menu, MF_STRING, kTraySettings, L"Einstellungen");
    AppendMenuW(menu, MF_STRING, kTrayMoveOverlay, L"Overlay verschieben");
    AppendMenuW(menu, MF_STRING, kTrayOpenUrl, L"Helltides öffnen");
    AppendMenuW(menu, MF_STRING, kTrayResetPanic, L"Panic-Stop zurücksetzen");
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr); AppendMenuW(menu, MF_STRING, kTrayExit, L"Beenden");
    SetForegroundWindow(window_);
    const auto command = TrackPopupMenu(menu, TPM_RETURNCMD | TPM_NONOTIFY, point.x, point.y, 0, window_, nullptr);
    DestroyMenu(menu);
    switch (command) {
    case kTrayShow: SetVisible(!IsWindowVisible(window_)); break;
    case kTraySettings: SetVisible(true); ui_.OpenSettings(); break;
    case kTrayMoveOverlay: overlay_.BeginMove(); break;
    case kTrayOpenUrl: ShellExecuteW(nullptr, L"open", L"https://helltides.com/", nullptr, nullptr, SW_SHOWNORMAL); break;
    case kTrayResetPanic: domain::disablePanicStop(); settings_ = domain::loadSettings(); previousNowMs_.reset(); Refresh(true); break;
    case kTrayExit: DestroyWindow(window_); break;
    default: break;
    }
}

LRESULT CALLBACK NativeApp::WindowProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam) {
    auto* self = reinterpret_cast<NativeApp*>(GetWindowLongPtrW(window, GWLP_USERDATA));
    if (message == WM_NCCREATE) {
        self = static_cast<NativeApp*>(reinterpret_cast<CREATESTRUCTW*>(lParam)->lpCreateParams);
        SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self)); self->window_ = window;
    }
    return self ? self->HandleMessage(message, wParam, lParam) : DefWindowProcW(window, message, wParam, lParam);
}

LRESULT NativeApp::HandleMessage(UINT message, WPARAM wParam, LPARAM lParam) {
    if (message == kTrayMessage) {
        if (lParam == WM_LBUTTONDBLCLK) SetVisible(!IsWindowVisible(window_));
        if (lParam == WM_RBUTTONUP || lParam == WM_CONTEXTMENU) ShowTrayMenu();
        return 0;
    }
    switch (message) {
    case WM_TIMER: if (wParam == kTimerId) Tick(); return 0;
    case WM_PAINT: { PAINTSTRUCT paint{}; BeginPaint(window_, &paint); ui_.Render(); EndPaint(window_, &paint); return 0; }
    case WM_ERASEBKGND: return 1;
    case WM_SIZE: ui_.HandleMessage(message, wParam, lParam); return 0;
    case WM_DISPLAYCHANGE:
    case WM_DPICHANGED: ui_.HandleMessage(message, wParam, lParam); return 0;
    case WM_LBUTTONDOWN:
    case WM_LBUTTONUP:
    case WM_MOUSEMOVE:
    case WM_KEYDOWN:
        if (ui_.HandleMessage(message, wParam, lParam)) return 0;
        return DefWindowProcW(window_, message, wParam, lParam);
    case WM_CLOSE: SetVisible(false); return 0;
    case WM_DESTROY: PostQuitMessage(0); return 0;
    default: return DefWindowProcW(window_, message, wParam, lParam);
    }
}

} // namespace helltime::integration
