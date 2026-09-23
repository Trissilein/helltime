#include "../src/domain/safety.h"
#include "../src/domain/fired_state.h"
#include "../src/domain/schedule.h"
#include "../src/domain/settings.h"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <array>
#include <cassert>
#include <cstdint>
#include <string>

using namespace helltime::domain;

namespace {

void writeRaw(const std::wstring& path, const std::string& value) {
    const auto handle = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    assert(handle != INVALID_HANDLE_VALUE);
    DWORD written = 0;
    assert(WriteFile(handle, value.data(), static_cast<DWORD>(value.size()), &written, nullptr) != FALSE);
    assert(written == value.size());
    CloseHandle(handle);
}

std::wstring temporaryLocalAppData() {
    wchar_t tempPath[MAX_PATH]{};
    assert(GetTempPathW(MAX_PATH, tempPath) != 0);
    wchar_t uniquePath[MAX_PATH]{};
    assert(GetTempFileNameW(tempPath, L"ht", 0, uniquePath) != 0);
    DeleteFileW(uniquePath);
    assert(CreateDirectoryW(uniquePath, nullptr) != FALSE);
    return uniquePath;
}

std::size_t utf8CodePointCount(const std::string& value) {
    std::size_t count = 0;
    for (std::size_t index = 0; index < value.size();) {
        const auto byte = static_cast<unsigned char>(value[index]);
        const std::size_t width = byte < 0x80 ? 1 : (byte & 0xE0) == 0xC0 ? 2 : (byte & 0xF0) == 0xE0 ? 3 : 4;
        assert(width <= 4 && index + width <= value.size());
        for (std::size_t continuation = 1; continuation < width; ++continuation) {
            assert((static_cast<unsigned char>(value[index + continuation]) & 0xC0) == 0x80);
        }
        ++count;
        index += width;
    }
    return count;
}

} // namespace

int main() {
    const auto oldSize = GetEnvironmentVariableW(L"LOCALAPPDATA", nullptr, 0);
    std::wstring oldLocalAppData(oldSize, L'\0');
    if (oldSize != 0) GetEnvironmentVariableW(L"LOCALAPPDATA", oldLocalAppData.data(), oldSize);

    const auto testLocalAppData = temporaryLocalAppData();
    assert(SetEnvironmentVariableW(L"LOCALAPPDATA", testLocalAppData.c_str()) != FALSE);
    disablePanicStop();

    const auto fixtureNow = static_cast<std::int64_t>(1789588200) * 1000;
    const auto schedule = generateSchedule(fixtureNow);
    assert(!schedule.helltide.empty() && schedule.helltide.front().timestamp == 1789585200);
    assert(schedule.legion.front().timestamp == 1789588200);
    assert(schedule.worldBoss.front().timestamp == 1789588800);
    assert(schedule.legion[1].timestamp - schedule.legion[0].timestamp == LEGION_INTERVAL_SECONDS);
    assert(schedule.worldBoss[1].timestamp - schedule.worldBoss[0].timestamp == WORLD_BOSS_INTERVAL_SECONDS);

    const auto active = findActiveOrNextHelltide(schedule.helltide, fixtureNow - 1'000);
    assert(active && active->active && active->targetMs == fixtureNow + 5 * 60'000);
    const auto next = findActiveOrNextHelltide(schedule.helltide, fixtureNow + 5 * 60'000);
    assert(next && !next->active && next->item.timestamp == fixtureNow / 1000 + 600);

    const ScheduleItem event{0, 1789592400, ScheduleType::Legion};
    const std::array<int, 3> reminders{10, 8, 5};
    const std::array<int, 1> fiveMinuteReminder{5};
    const auto caughtUp = findDueReminderTimers(event, reminders, 3, 1789592160000, 1789591740000);
    assert(caughtUp.catchUp && caughtUp.due.size() == 2);
    assert(caughtUp.due[0].index == 1 && caughtUp.due[1].index == 2);
    assert(findDueReminderTimers(event, fiveMinuteReminder, 1, 1789592101000, 1789592099000).due.size() == 1);
    assert(findDueReminderTimers(event, reminders, 3, 1789592401000, 1789592399000).due.empty());
    assert(findDueReminderTimers(event, reminders, 3, 1789592040000, 1789592100000).due.empty());

    const auto defaults = defaultSettings();
    assert(defaults.version == 6 && defaults.categories[0].timerCount == 3);
    assert(defaults.categories[0].ttsName == "H\xC3\xB6llenhochwasser");
    assert(settingsFilePath().find(L"HelltimeNative\\settings.json") != std::wstring::npos);
    assert(saveSettings(defaults));

    const auto path = settingsFilePath();
    writeRaw(path, "{");
    const auto malformed = loadSettings();
    assert(malformed.version == 6 && malformed.volume == 0.8);

    writeRaw(path,
        "{\"version\":6,\"volume\":-2,\"overlayBgHex\":\"nope\",\"overlayScaleX\":9,"
        "\"overlayScaleY\":0,\"overlayBgOpacity\":-1,\"overlayLineBgOpacity\":3,"
        "\"categories\":{\"helltide\":{\"timerCount\":9,\"ttsName\":\"abcdefghijklmnopqrstuvwxyzabcdefghijklmnopqrstuvwxyzabcdefghijklmnopqrstuvwxyzabcdefghijklmnopqrstuvwxyz\","
        "\"timers\":[{\"minutesBefore\":0,\"pitchHz\":3000}]}}}");
    const auto clamped = loadSettings();
    assert(clamped.volume == 0 && clamped.overlayBgHex == "#0b1220");
    assert(clamped.overlayScaleX == 2 && clamped.overlayScaleY == 0.6);
    assert(clamped.overlayBgOpacity == 0 && clamped.overlayLineBgOpacity == 1);
    assert(clamped.categories[0].timerCount == 3 && clamped.categories[0].timers[0].minutesBefore == 0);
    assert(clamped.categories[0].timers[0].pitchHz == 2000 && clamped.categories[0].ttsName.size() == 80);

    // Native keeps the approved range and step values across a save/reload.
    auto snapped = defaultSettings();
    snapped.categories[0].timers[0].minutesBefore = 0;
    snapped.categories[0].timers[0].pitchHz = 249;
    assert(saveSettings(snapped));
    auto roundTrip = loadSettings();
    assert(roundTrip.categories[0].timers[0].minutesBefore == 0);
    assert(roundTrip.categories[0].timers[0].pitchHz == 200);

    snapped.categories[0].timers[0].minutesBefore = 7;
    snapped.categories[0].timers[0].pitchHz = 250;
    assert(saveSettings(snapped));
    roundTrip = loadSettings();
    assert(roundTrip.categories[0].timers[0].minutesBefore == 5);
    assert(roundTrip.categories[0].timers[0].pitchHz == 300);

    snapped.categories[0].timers[0].minutesBefore = 8;
    snapped.categories[0].timers[0].pitchHz = 351;
    assert(saveSettings(snapped));
    roundTrip = loadSettings();
    assert(roundTrip.categories[0].timers[0].minutesBefore == 10);
    assert(roundTrip.categories[0].timers[0].pitchHz == 400);

    // UTF-8 TTS names are limited by code points, not raw bytes, and survive
    // the JSON round-trip without being cut in the middle of a character.
    std::string unicodeName;
    for (int index = 0; index < 80; ++index) unicodeName += "\xC3\xA4";
    writeRaw(path, "{\"version\":6,\"categories\":{\"helltide\":{\"ttsName\":\"" + unicodeName + "\"}}}");
    const auto unicodeSettings = loadSettings();
    assert(unicodeSettings.categories[0].ttsName.size() == 160);
    assert(utf8CodePointCount(unicodeSettings.categories[0].ttsName) == 80);
    assert(saveSettings(unicodeSettings));
    const auto unicodeReloaded = loadSettings();
    assert(unicodeReloaded.categories[0].ttsName == unicodeSettings.categories[0].ttsName);

    const auto firedPath = firedStateFilePath();
    assert(firedPath.find(L"HelltimeNative\\fired.json") != std::wstring::npos);
    DeleteFileW(firedPath.c_str());
    FiredState fired;
    assert(markFiredIfFresh(fired, "legion:123:0", fixtureNow));
    assert(hasFreshFired(fired, "legion:123:0", fixtureNow + 11 * 60 * 60 * 1000));
    assert(!markFiredIfFresh(fired, "legion:123:0", fixtureNow + 1'000));
    assert(markFiredIfFresh(fired, "legion:123:0", fixtureNow + FIRED_STATE_RETENTION_MS + 1));
    assert(saveFiredState(fired));
    const auto reloadedFired = loadFiredState();
    assert(reloadedFired.size() == 1 && reloadedFired.at("legion:123:0") == fixtureNow + FIRED_STATE_RETENTION_MS + 1);

    FiredState pruning{
        {"old:1:0", fixtureNow - 1},
        {"new:2:1", fixtureNow + FIRED_STATE_RETENTION_MS},
        {"future:3:2", fixtureNow + FIRED_STATE_RETENTION_MS + 1},
    };
    pruneFiredState(pruning, fixtureNow + FIRED_STATE_RETENTION_MS);
    assert(pruning.size() == 2 && pruning.contains("new:2:1") && pruning.contains("future:3:2"));

    writeRaw(firedPath, "{\"helltide:7:0\":1789588200000,\"world_boss:8:2\":1789588200123}");
    const auto parsedFired = loadFiredState();
    assert(parsedFired.size() == 2 && parsedFired.at("world_boss:8:2") == 1789588200123);
    writeRaw(firedPath, "{\"broken\":999999999999999999999999}");
    assert(loadFiredState().empty());
    writeRaw(firedPath, "{");
    assert(loadFiredState().empty());
    writeRaw(firedPath, "");
    assert(loadFiredState().empty());
    std::string oversized(64 * 1024 + 1, 'x');
    writeRaw(firedPath, oversized);
    assert(loadFiredState().empty());

    enablePanicStop();
    const auto panic = loadSettings();
    assert(isPanicStopEnabled() && !panic.overlayWindowEnabled && !panic.soundEnabled && !panic.autoRefreshEnabled);
    disablePanicStop();
    assert(!isPanicStopEnabled());

    DeleteFileW(path.c_str());
    DeleteFileW(firedPath.c_str());
    DeleteFileW((firedPath + L".tmp").c_str());
    RemoveDirectoryW((testLocalAppData + L"\\HelltimeNative").c_str());
    RemoveDirectoryW(testLocalAppData.c_str());
    assert(SetEnvironmentVariableW(L"LOCALAPPDATA", oldLocalAppData.empty() ? nullptr : oldLocalAppData.c_str()) != FALSE);
    return 0;
}
