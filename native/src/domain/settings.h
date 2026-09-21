#pragma once

#include <array>
#include <string>

namespace helltime::domain {

enum class BeepPattern {
    Beep,
    Double,
    Triple,
};

enum class OverlayWindowMode {
    Overview,
    Toast,
};

struct TimerSettings {
    int minutesBefore{30};
    bool ttsEnabled{true};
    BeepPattern beepPattern{BeepPattern::Beep};
    int pitchHz{880};
};

struct CategorySettings {
    bool enabled{true};
    std::string ttsName;
    int timerCount{3};
    std::array<TimerSettings, 3> timers{};
};

struct Settings {
    int version{6};
    double volume{0.8};
    bool systemToastsEnabled{false};
    bool soundEnabled{true};
    bool autoRefreshEnabled{false};
    bool overlayWindowEnabled{true};
    OverlayWindowMode overlayWindowMode{OverlayWindowMode::Overview};
    std::array<bool, 3> overlayWindowCategories{true, true, true};
    std::string overlayBgHex{"#0b1220"};
    double overlayScaleX{1.0};
    double overlayScaleY{1.0};
    double overlayBgOpacity{0.2};
    double overlayLineBgOpacity{0.72};
    std::array<CategorySettings, 3> categories{};
};

Settings defaultSettings();
Settings normalizeSettings(const Settings& settings);

// Native store is intentionally independent from WebView/localStorage data.
std::wstring settingsFilePath();
Settings loadSettings();
bool saveSettings(const Settings& settings);

} // namespace helltime::domain
