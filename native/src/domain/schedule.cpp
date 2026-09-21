#include "schedule.h"

#include <algorithm>
#include <chrono>
#include <limits>

namespace helltime::domain {
namespace {

constexpr std::int64_t kReminderFireWindowMs = 30'000;
constexpr std::int64_t kReminderCatchUpWindowMs = 5 * 60'000;

// C++ integer division truncates toward zero; JavaScript Math.floor/ceil do not.
std::int64_t floorDiv(std::int64_t numerator, std::int64_t denominator) {
    const auto quotient = numerator / denominator;
    const auto remainder = numerator % denominator;
    return remainder < 0 ? quotient - 1 : quotient;
}

std::int64_t ceilDiv(std::int64_t numerator, std::int64_t denominator) {
    const auto quotient = numerator / denominator;
    const auto remainder = numerator % denominator;
    return remainder > 0 ? quotient + 1 : quotient;
}

std::int64_t firstOccurrence(
    std::int64_t nowSeconds,
    std::int64_t anchorSeconds,
    std::int64_t intervalSeconds,
    bool includeCurrent) {
    const auto offset = nowSeconds - anchorSeconds;
    const auto steps = std::max<std::int64_t>(
        0,
        includeCurrent ? floorDiv(offset, intervalSeconds) : ceilDiv(offset, intervalSeconds));
    return anchorSeconds + steps * intervalSeconds;
}

void appendOccurrences(
    std::vector<ScheduleItem>& output,
    ScheduleType type,
    std::int64_t nowSeconds,
    std::int64_t anchorSeconds,
    std::int64_t intervalSeconds,
    std::int64_t horizonSeconds,
    bool includeCurrent) {
    const auto endSeconds = nowSeconds + horizonSeconds;
    for (auto timestamp = firstOccurrence(nowSeconds, anchorSeconds, intervalSeconds, includeCurrent);
         timestamp <= endSeconds;
         timestamp += intervalSeconds) {
        output.push_back({timestamp, timestamp, type});
        // Keep malformed caller horizons from turning into an infinite loop at integer overflow.
        if (timestamp > std::numeric_limits<std::int64_t>::max() - intervalSeconds) break;
    }
}

} // namespace

std::int64_t nowMilliseconds() {
    const auto now = std::chrono::system_clock::now().time_since_epoch();
    return std::chrono::duration_cast<std::chrono::milliseconds>(now).count();
}

Schedule generateSchedule(std::int64_t nowMs, std::int64_t horizonSeconds) {
    // nowMs is integral here, so this is Math.floor(nowMs / 1000), including pre-epoch values.
    const auto nowSeconds = floorDiv(nowMs, 1000);
    Schedule result;
    appendOccurrences(
        result.helltide,
        ScheduleType::Helltide,
        nowSeconds,
        0,
        HELLTIDE_INTERVAL_SECONDS,
        horizonSeconds,
        true);
    appendOccurrences(
        result.legion,
        ScheduleType::Legion,
        nowSeconds,
        LEGION_ANCHOR_SECONDS,
        LEGION_INTERVAL_SECONDS,
        horizonSeconds,
        false);
    appendOccurrences(
        result.worldBoss,
        ScheduleType::WorldBoss,
        nowSeconds,
        WORLD_BOSS_ANCHOR_SECONDS,
        WORLD_BOSS_INTERVAL_SECONDS,
        horizonSeconds,
        false);
    return result;
}

std::optional<ScheduleItem> findNext(const std::vector<ScheduleItem>& items, std::int64_t nowMs) {
    std::optional<ScheduleItem> best;
    std::int64_t bestStartMs = std::numeric_limits<std::int64_t>::max();
    for (const auto& item : items) {
        const auto startMs = item.timestamp * 1000;
        if (startMs <= nowMs || startMs >= bestStartMs) continue;
        best = item;
        bestStartMs = startMs;
    }
    return best;
}

std::optional<EventTiming> findActiveOrNextHelltide(
    const std::vector<ScheduleItem>& items,
    std::int64_t nowMs) {
    std::optional<EventTiming> next;
    for (const auto& item : items) {
        const auto startMs = item.timestamp * 1000;
        const auto endMs = startMs + HELLTIDE_DURATION_SECONDS * 1000;
        if (startMs <= nowMs && nowMs < endMs) {
            return EventTiming{item, startMs, endMs, true};
        }
        if (startMs > nowMs && (!next || startMs < next->startMs)) {
            next = EventTiming{item, startMs, startMs, false};
        }
    }
    return next;
}

std::optional<EventTiming> toUpcomingTiming(const std::optional<ScheduleItem>& item) {
    if (!item) return std::nullopt;
    const auto startMs = item->timestamp * 1000;
    return EventTiming{*item, startMs, startMs, false};
}

DueReminderResult findDueReminderTimers(
    const ScheduleItem& item,
    std::span<const int> minutesBefore,
    int timerCount,
    std::int64_t nowMs,
    std::optional<std::int64_t> previousNowMs) {
    DueReminderResult result;
    const auto startMs = item.timestamp * 1000;
    if (startMs <= nowMs || !previousNowMs || nowMs < *previousNowMs) return result;

    result.catchUp = nowMs - *previousNowMs > kReminderFireWindowMs;
    const auto count = std::min<std::size_t>(
        timerCount < 0 ? 0u : static_cast<std::size_t>(timerCount), minutesBefore.size());
    for (std::size_t index = 0; index < count; ++index) {
        const auto triggerMs = startMs - static_cast<std::int64_t>(minutesBefore[index]) * 60'000;
        if (triggerMs <= *previousNowMs || triggerMs > nowMs) continue;
        if (result.catchUp && nowMs - triggerMs > kReminderCatchUpWindowMs) continue;
        result.due.push_back({static_cast<int>(index), triggerMs});
    }
    return result;
}

} // namespace helltime::domain
