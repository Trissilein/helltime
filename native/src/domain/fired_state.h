#pragma once

#include <cstdint>
#include <map>
#include <string>
#include <string_view>

namespace helltime::domain {

// Matches the retention window used by the Tauri implementation.
inline constexpr std::int64_t FIRED_STATE_RETENTION_MS = 12 * 60 * 60 * 1000;

using FiredState = std::map<std::string, std::int64_t>;

// Returns %LocalAppData%\\HelltimeNative\\fired.json.
std::wstring firedStateFilePath();

// Missing, malformed, or oversized files produce an empty state.
FiredState loadFiredState();

// Writes the state using a flushed temporary file and an atomic replacement.
bool saveFiredState(const FiredState& state);

// Removes entries older than the 12-hour retention window.
void pruneFiredState(FiredState& state, std::int64_t nowMs);

// True when key was fired within the retention window at nowMs.
bool hasFreshFired(const FiredState& state, std::string_view key, std::int64_t nowMs);

// Marks key at nowMs only when no fresh mark exists. Returns true when inserted.
bool markFiredIfFresh(FiredState& state, std::string_view key, std::int64_t nowMs);

} // namespace helltime::domain
