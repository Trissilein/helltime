#pragma once

#include <string>

namespace helltime::domain {

/** Process-safe panic flag. The flag survives restart in the native app data directory. */
bool isPanicStopEnabled();
void enablePanicStop();
void disablePanicStop();

// Exposed for diagnostics/tests; returns %LocalAppData%\\HelltimeNative\\panic-stop.flag.
std::wstring panicStopFilePath();

} // namespace helltime::domain
