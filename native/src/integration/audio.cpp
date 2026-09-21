#include "audio.h"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <sapi.h>
#include <mmsystem.h>

#include <algorithm>
#include <cmath>
#include <string>
#include <thread>
#include <vector>

#pragma comment(lib, "sapi.lib")
#pragma comment(lib, "winmm.lib")

namespace helltime::integration {
namespace {

std::wstring utf8ToWide(const std::string& value) {
    if (value.empty()) return {};
    const int count = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, value.data(), static_cast<int>(value.size()), nullptr, 0);
    if (count <= 0) return std::wstring(value.begin(), value.end());
    std::wstring result(static_cast<std::size_t>(count), L'\0');
    MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, value.data(), static_cast<int>(value.size()), result.data(), count);
    return result;
}

void playTone(int pitchHz, int durationMs, double volume) {
    if (volume <= 0.001 || pitchHz < 20) return;
    constexpr int sampleRate = 44100;
    const auto samples = static_cast<std::size_t>(sampleRate * durationMs / 1000);
    std::vector<short> pcm(samples);
    const auto gain = std::clamp(volume, 0.0, 1.0) * 0.28;
    for (std::size_t i = 0; i < samples; ++i) {
        const auto envelope = i < 240 ? static_cast<double>(i) / 240.0
            : i + 240 > samples ? static_cast<double>(samples - i) / 240.0 : 1.0;
        pcm[i] = static_cast<short>(std::sin(6.283185307179586 * pitchHz * static_cast<double>(i) / sampleRate) * 32767.0 * gain * std::clamp(envelope, 0.0, 1.0));
    }

    WAVEFORMATEX format{};
    format.wFormatTag = WAVE_FORMAT_PCM;
    format.nChannels = 1;
    format.nSamplesPerSec = sampleRate;
    format.wBitsPerSample = 16;
    format.nBlockAlign = format.nChannels * format.wBitsPerSample / 8;
    format.nAvgBytesPerSec = format.nSamplesPerSec * format.nBlockAlign;

    HWAVEOUT output = nullptr;
    if (waveOutOpen(&output, WAVE_MAPPER, &format, 0, 0, CALLBACK_NULL) != MMSYSERR_NOERROR) return;
    WAVEHDR header{};
    header.lpData = reinterpret_cast<LPSTR>(pcm.data());
    header.dwBufferLength = static_cast<DWORD>(pcm.size() * sizeof(short));
    if (waveOutPrepareHeader(output, &header, sizeof(header)) == MMSYSERR_NOERROR) {
        waveOutWrite(output, &header, sizeof(header));
        while ((header.dwFlags & WHDR_DONE) == 0) Sleep(2);
        waveOutUnprepareHeader(output, &header, sizeof(header));
    }
    waveOutClose(output);
}

} // namespace

void SpeakReminder(const std::string& text, int volumePercent) {
    const auto wide = utf8ToWide(text);
    const auto volume = static_cast<USHORT>(std::clamp(volumePercent, 0, 100));
    std::thread([wide, volume] {
        const auto initialized = SUCCEEDED(CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED));
        ISpVoice* voice = nullptr;
        if (SUCCEEDED(CoCreateInstance(CLSID_SpVoice, nullptr, CLSCTX_ALL, IID_ISpVoice, reinterpret_cast<void**>(&voice)))) {
            voice->SetVolume(volume);
            // Keep voice alive until synchronous Speak completes. Worker keeps UI timer responsive.
            voice->Speak(wide.c_str(), SPF_DEFAULT, nullptr);
            voice->Release();
        }
        if (initialized) CoUninitialize();
    }).detach();
}

void PlayReminderBeep(domain::BeepPattern pattern, int pitchHz, double volume) {
    const int count = pattern == domain::BeepPattern::Triple ? 3 : pattern == domain::BeepPattern::Double ? 2 : 1;
    for (int i = 0; i < count; ++i) {
        playTone(pitchHz, 120, volume);
        if (i + 1 < count) Sleep(65);
    }
}

} // namespace helltime::integration
