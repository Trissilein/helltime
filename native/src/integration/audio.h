#pragma once

#include "../domain/settings.h"

#include <string>

namespace helltime::integration {

void SpeakReminder(const std::string& text, int volumePercent);
void PlayReminderBeep(domain::BeepPattern pattern, int pitchHz, double volume);

} // namespace helltime::integration
