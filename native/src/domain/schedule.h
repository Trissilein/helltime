#pragma once

#include <cstdint>
#include <optional>
#include <span>
#include <vector>

namespace helltime::domain {

enum class ScheduleType {
    Helltide,
    Legion,
    WorldBoss,
};

struct ScheduleItem {
    std::int64_t id{};
    std::int64_t timestamp{}; // UTC seconds since Unix epoch.
    ScheduleType type{};
};

struct Schedule {
    std::vector<ScheduleItem> helltide;
    std::vector<ScheduleItem> legion;
    std::vector<ScheduleItem> worldBoss;
};

inline constexpr std::int64_t HELLTIDE_INTERVAL_SECONDS = 60 * 60;
inline constexpr std::int64_t HELLTIDE_DURATION_SECONDS = 55 * 60;
inline constexpr std::int64_t LEGION_INTERVAL_SECONDS = 25 * 60;
inline constexpr std::int64_t WORLD_BOSS_INTERVAL_SECONDS = 12600;
inline constexpr std::int64_t SCHEDULE_HORIZON_SECONDS = 48 * 60 * 60;
inline constexpr std::int64_t LEGION_ANCHOR_SECONDS = 1789588200;
inline constexpr std::int64_t WORLD_BOSS_ANCHOR_SECONDS = 1789588800;

std::int64_t nowMilliseconds();

/** Generate deterministic UTC event times for the requested horizon. */
Schedule generateSchedule(
    std::int64_t nowMs = nowMilliseconds(),
    std::int64_t horizonSeconds = SCHEDULE_HORIZON_SECONDS);

std::optional<ScheduleItem> findNext(const std::vector<ScheduleItem>& items, std::int64_t nowMs);

struct EventTiming {
    ScheduleItem item;
    std::int64_t startMs{};
    std::int64_t targetMs{};
    bool active{};
};

std::optional<EventTiming> findActiveOrNextHelltide(
    const std::vector<ScheduleItem>& items,
    std::int64_t nowMs);

std::optional<EventTiming> toUpcomingTiming(const std::optional<ScheduleItem>& item);

struct DueReminderTimer {
    int index{};
    std::int64_t triggerMs{};
};

struct DueReminderResult {
    std::vector<DueReminderTimer> due;
    bool catchUp{};
};

/** Find reminder thresholds crossed since previous wall-clock observation. */
DueReminderResult findDueReminderTimers(
    const ScheduleItem& item,
    std::span<const int> minutesBefore,
    int timerCount,
    std::int64_t nowMs,
    std::optional<std::int64_t> previousNowMs);

} // namespace helltime::domain
