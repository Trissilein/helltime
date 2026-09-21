#include "safety.h"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <atomic>

namespace helltime::domain {
namespace {

std::atomic_bool g_panicStop{false};

std::wstring localAppData() {
    std::wstring buffer(32768, L'\0');
    const auto size = GetEnvironmentVariableW(L"LOCALAPPDATA", buffer.data(), static_cast<DWORD>(buffer.size()));
    if (size == 0 || size >= buffer.size()) return {};
    buffer.resize(size);
    return buffer;
}

std::wstring appDataDirectory() {
    const auto base = localAppData();
    return base.empty() ? std::wstring{} : base + L"\\HelltimeNative";
}

void ensureAppDataDirectory() {
    const auto directory = appDataDirectory();
    if (!directory.empty()) CreateDirectoryW(directory.c_str(), nullptr);
}

} // namespace

std::wstring panicStopFilePath() {
    const auto directory = appDataDirectory();
    return directory.empty() ? std::wstring{} : directory + L"\\panic-stop.flag";
}

bool isPanicStopEnabled() {
    if (g_panicStop.load(std::memory_order_relaxed)) return true;
    const auto path = panicStopFilePath();
    return !path.empty() && GetFileAttributesW(path.c_str()) != INVALID_FILE_ATTRIBUTES;
}

void enablePanicStop() {
    g_panicStop.store(true, std::memory_order_relaxed);
    ensureAppDataDirectory();
    const auto path = panicStopFilePath();
    if (path.empty()) return;
    const auto handle = CreateFileW(
        path.c_str(),
        GENERIC_WRITE,
        0,
        nullptr,
        CREATE_ALWAYS,
        FILE_ATTRIBUTE_HIDDEN,
        nullptr);
    if (handle != INVALID_HANDLE_VALUE) CloseHandle(handle);
}

void disablePanicStop() {
    g_panicStop.store(false, std::memory_order_relaxed);
    const auto path = panicStopFilePath();
    if (!path.empty()) DeleteFileW(path.c_str());
}

} // namespace helltime::domain
