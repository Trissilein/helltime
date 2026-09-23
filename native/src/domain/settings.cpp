#include "settings.h"

#include "safety.h"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdlib>
#include <iomanip>
#include <limits>
#include <map>
#include <sstream>
#include <string_view>
#include <utility>
#include <vector>

namespace helltime::domain {
namespace {

constexpr std::size_t kMaxJsonBytes = 64 * 1024;
constexpr int kMaxJsonDepth = 8;

struct JsonValue {
    enum class Kind { Null, Boolean, Number, String, Array, Object } kind{Kind::Null};
    bool boolean{};
    double number{};
    std::string string;
    std::vector<JsonValue> array;
    std::map<std::string, JsonValue> object;
};

class JsonParser {
public:
    explicit JsonParser(std::string_view input) : input_(input) {}

    bool parse(JsonValue& output) {
        skipWhitespace();
        if (!parseValue(output, 0)) return false;
        skipWhitespace();
        return position_ == input_.size();
    }

private:
    std::string_view input_;
    std::size_t position_{};

    void skipWhitespace() {
        while (position_ < input_.size()) {
            const auto c = static_cast<unsigned char>(input_[position_]);
            if (c != ' ' && c != '\t' && c != '\r' && c != '\n') break;
            ++position_;
        }
    }

    bool consume(char expected) {
        if (position_ >= input_.size() || input_[position_] != expected) return false;
        ++position_;
        return true;
    }

    bool parseValue(JsonValue& value, int depth) {
        if (depth > kMaxJsonDepth || position_ >= input_.size()) return false;
        switch (input_[position_]) {
        case 'n':
            if (input_.substr(position_, 4) != "null") return false;
            position_ += 4;
            value = {};
            return true;
        case 't':
            if (input_.substr(position_, 4) != "true") return false;
            position_ += 4;
            value = {};
            value.kind = JsonValue::Kind::Boolean;
            value.boolean = true;
            return true;
        case 'f':
            if (input_.substr(position_, 5) != "false") return false;
            position_ += 5;
            value = {};
            value.kind = JsonValue::Kind::Boolean;
            value.boolean = false;
            return true;
        case '"':
            value = {};
            value.kind = JsonValue::Kind::String;
            return parseString(value.string);
        case '[':
            return parseArray(value, depth);
        case '{':
            return parseObject(value, depth);
        default:
            return parseNumber(value);
        }
    }

    static bool hexDigit(char c, unsigned int& value) {
        if (c >= '0' && c <= '9') value = static_cast<unsigned int>(c - '0');
        else if (c >= 'a' && c <= 'f') value = static_cast<unsigned int>(c - 'a' + 10);
        else if (c >= 'A' && c <= 'F') value = static_cast<unsigned int>(c - 'A' + 10);
        else return false;
        return true;
    }

    static void appendUtf8(std::string& output, unsigned int codepoint) {
        if (codepoint <= 0x7F) {
            output.push_back(static_cast<char>(codepoint));
        } else if (codepoint <= 0x7FF) {
            output.push_back(static_cast<char>(0xC0 | (codepoint >> 6)));
            output.push_back(static_cast<char>(0x80 | (codepoint & 0x3F)));
        } else if (codepoint <= 0xFFFF) {
            output.push_back(static_cast<char>(0xE0 | (codepoint >> 12)));
            output.push_back(static_cast<char>(0x80 | ((codepoint >> 6) & 0x3F)));
            output.push_back(static_cast<char>(0x80 | (codepoint & 0x3F)));
        } else {
            output.push_back(static_cast<char>(0xF0 | (codepoint >> 18)));
            output.push_back(static_cast<char>(0x80 | ((codepoint >> 12) & 0x3F)));
            output.push_back(static_cast<char>(0x80 | ((codepoint >> 6) & 0x3F)));
            output.push_back(static_cast<char>(0x80 | (codepoint & 0x3F)));
        }
    }

    bool parseUnicodeEscape(std::string& output) {
        if (position_ + 4 > input_.size()) return false;
        unsigned int codepoint = 0;
        for (int i = 0; i < 4; ++i) {
            unsigned int digit = 0;
            if (!hexDigit(input_[position_++], digit)) return false;
            codepoint = (codepoint << 4) | digit;
        }

        if (codepoint >= 0xD800 && codepoint <= 0xDBFF &&
            position_ + 6 <= input_.size() && input_[position_] == '\\' && input_[position_ + 1] == 'u') {
            std::size_t saved = position_;
            position_ += 2;
            unsigned int low = 0;
            bool validLow = true;
            for (int i = 0; i < 4; ++i) {
                unsigned int digit = 0;
                if (!hexDigit(input_[position_++], digit)) {
                    validLow = false;
                    break;
                }
                low = (low << 4) | digit;
            }
            if (validLow && low >= 0xDC00 && low <= 0xDFFF) {
                codepoint = 0x10000 + ((codepoint - 0xD800) << 10) + (low - 0xDC00);
            } else {
                position_ = saved;
            }
        }
        appendUtf8(output, codepoint);
        return true;
    }

    bool parseString(std::string& output) {
        if (!consume('"')) return false;
        output.clear();
        while (position_ < input_.size()) {
            const auto c = input_[position_++];
            if (c == '"') return true;
            if (static_cast<unsigned char>(c) < 0x20) return false;
            if (c != '\\') {
                output.push_back(c);
                continue;
            }
            if (position_ >= input_.size()) return false;
            switch (input_[position_++]) {
            case '"': output.push_back('"'); break;
            case '\\': output.push_back('\\'); break;
            case '/': output.push_back('/'); break;
            case 'b': output.push_back('\b'); break;
            case 'f': output.push_back('\f'); break;
            case 'n': output.push_back('\n'); break;
            case 'r': output.push_back('\r'); break;
            case 't': output.push_back('\t'); break;
            case 'u': if (!parseUnicodeEscape(output)) return false; break;
            default: return false;
            }
        }
        return false;
    }

    bool parseNumber(JsonValue& value) {
        const auto start = position_;
        if (position_ < input_.size() && input_[position_] == '-') ++position_;
        if (position_ >= input_.size()) return false;
        if (input_[position_] == '0') {
            ++position_;
        } else {
            if (input_[position_] < '1' || input_[position_] > '9') return false;
            while (position_ < input_.size() && input_[position_] >= '0' && input_[position_] <= '9') ++position_;
        }
        if (position_ < input_.size() && input_[position_] == '.') {
            ++position_;
            const auto fractionStart = position_;
            while (position_ < input_.size() && input_[position_] >= '0' && input_[position_] <= '9') ++position_;
            if (position_ == fractionStart) return false;
        }
        if (position_ < input_.size() && (input_[position_] == 'e' || input_[position_] == 'E')) {
            ++position_;
            if (position_ < input_.size() && (input_[position_] == '+' || input_[position_] == '-')) ++position_;
            const auto exponentStart = position_;
            while (position_ < input_.size() && input_[position_] >= '0' && input_[position_] <= '9') ++position_;
            if (position_ == exponentStart) return false;
        }
        const auto token = std::string(input_.substr(start, position_ - start));
        char* end = nullptr;
        const auto number = std::strtod(token.c_str(), &end);
        if (end != token.c_str() + token.size() || !std::isfinite(number)) return false;
        value = {};
        value.kind = JsonValue::Kind::Number;
        value.number = number;
        return true;
    }

    bool parseArray(JsonValue& value, int depth) {
        if (!consume('[')) return false;
        value = {};
        value.kind = JsonValue::Kind::Array;
        skipWhitespace();
        if (consume(']')) return true;
        for (std::size_t count = 0; count < 64; ++count) {
            JsonValue child;
            skipWhitespace();
            if (!parseValue(child, depth + 1)) return false;
            value.array.push_back(std::move(child));
            skipWhitespace();
            if (consume(']')) return true;
            if (!consume(',')) return false;
        }
        return false;
    }

    bool parseObject(JsonValue& value, int depth) {
        if (!consume('{')) return false;
        value = {};
        value.kind = JsonValue::Kind::Object;
        skipWhitespace();
        if (consume('}')) return true;
        for (std::size_t count = 0; count < 128; ++count) {
            skipWhitespace();
            std::string key;
            if (!parseString(key)) return false;
            skipWhitespace();
            if (!consume(':')) return false;
            skipWhitespace();
            JsonValue child;
            if (!parseValue(child, depth + 1)) return false;
            value.object[std::move(key)] = std::move(child);
            skipWhitespace();
            if (consume('}')) return true;
            if (!consume(',')) return false;
        }
        return false;
    }
};

const JsonValue* member(const JsonValue& object, std::string_view name) {
    if (object.kind != JsonValue::Kind::Object) return nullptr;
    const auto it = object.object.find(std::string(name));
    return it == object.object.end() ? nullptr : &it->second;
}

bool jsonBool(const JsonValue* value, bool fallback) {
    return value && value->kind == JsonValue::Kind::Boolean ? value->boolean : fallback;
}

bool jsonNumber(const JsonValue* value, double& output) {
    if (!value || value->kind != JsonValue::Kind::Number || !std::isfinite(value->number)) return false;
    output = value->number;
    return true;
}

std::string jsonString(const JsonValue* value, std::string fallback) {
    return value && value->kind == JsonValue::Kind::String ? value->string : std::move(fallback);
}

double clampUnit(double value, double fallback) {
    return std::isfinite(value) ? std::clamp(value, 0.0, 1.0) : fallback;
}

double clampFloat(double value, double fallback, double minimum, double maximum) {
    return std::isfinite(value) ? std::clamp(value, minimum, maximum) : fallback;
}

int clampInt(double value, int fallback, int minimum, int maximum) {
    if (!std::isfinite(value)) return fallback;
    const auto rounded = static_cast<long long>(std::floor(value + 0.5));
    return static_cast<int>(std::clamp<long long>(rounded, minimum, maximum));
}

int clampStep(double value, int fallback, int minimum, int maximum, int step) {
    const auto clamped = clampInt(value, fallback, minimum, maximum);
    if (step <= 1) return clamped;
    const auto offset = clamped - minimum;
    const auto snapped = minimum + ((offset + step / 2) / step) * step;
    return std::clamp(snapped, minimum, maximum);
}

std::string trimAndLimit(std::string value) {
    auto isSpace = [](unsigned char c) { return c == ' ' || c == '\t' || c == '\r' || c == '\n' || c == '\f'; };
    auto first = value.begin();
    while (first != value.end() && isSpace(static_cast<unsigned char>(*first))) ++first;
    auto last = value.end();
    while (last != first && isSpace(static_cast<unsigned char>(*(last - 1)))) --last;
    value = std::string(first, last);
    if (value.size() <= 80) return value;
    // Keep UTF-8 valid while matching source's 80-character intent for ordinary text.
    std::size_t end = 0;
    int characters = 0;
    while (end < value.size() && characters < 80) {
        const auto byte = static_cast<unsigned char>(value[end]);
        const auto width = byte < 0x80 ? 1 : (byte & 0xE0) == 0xC0 ? 2 : (byte & 0xF0) == 0xE0 ? 3 : 4;
        if (end + width > value.size()) break;
        end += width;
        ++characters;
    }
    value.resize(end);
    return value;
}

bool validHexColor(const std::string& value) {
    if (value.size() != 7 || value[0] != '#') return false;
    for (std::size_t i = 1; i < value.size(); ++i) {
        const auto c = static_cast<unsigned char>(value[i]);
        if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F'))) return false;
    }
    return true;
}

std::string lowerHex(std::string value) {
    for (auto& c : value) {
        if (c >= 'A' && c <= 'F') c = static_cast<char>(c - 'A' + 'a');
    }
    return value;
}

BeepPattern beepPatternFromJson(const JsonValue* value, BeepPattern fallback) {
    if (!value || value->kind != JsonValue::Kind::String) return fallback;
    if (value->string == "beep") return BeepPattern::Beep;
    if (value->string == "double") return BeepPattern::Double;
    if (value->string == "triple") return BeepPattern::Triple;
    return fallback;
}

std::string beepPatternToJson(BeepPattern value) {
    switch (value) {
    case BeepPattern::Double: return "double";
    case BeepPattern::Triple: return "triple";
    case BeepPattern::Beep: return "beep";
    }
    return "beep";
}

TimerSettings normalizeTimer(const JsonValue* raw, const TimerSettings& fallback) {
    TimerSettings result = fallback;
    if (!raw || raw->kind != JsonValue::Kind::Object) return result;
    double number = 0;
    if (jsonNumber(member(*raw, "minutesBefore"), number)) result.minutesBefore = clampStep(number, fallback.minutesBefore, 0, 60, 5);
    result.ttsEnabled = jsonBool(member(*raw, "ttsEnabled"), fallback.ttsEnabled);
    result.beepPattern = beepPatternFromJson(member(*raw, "beepPattern"), fallback.beepPattern);
    if (jsonNumber(member(*raw, "pitchHz"), number)) result.pitchHz = clampStep(number, fallback.pitchHz, 200, 2000, 100);
    return result;
}

CategorySettings normalizeCategory(const JsonValue* raw, const CategorySettings& fallback) {
    CategorySettings result = fallback;
    if (!raw || raw->kind != JsonValue::Kind::Object) return result;
    result.enabled = jsonBool(member(*raw, "enabled"), fallback.enabled);
    result.ttsName = trimAndLimit(jsonString(member(*raw, "ttsName"), fallback.ttsName));
    double number = 0;
    if (jsonNumber(member(*raw, "timerCount"), number) && (number == 1 || number == 2 || number == 3)) {
        result.timerCount = static_cast<int>(number);
    }
    const auto* rawTimers = member(*raw, "timers");
    if (rawTimers && rawTimers->kind == JsonValue::Kind::Array) {
        for (std::size_t index = 0; index < result.timers.size() && index < rawTimers->array.size(); ++index) {
            result.timers[index] = normalizeTimer(&rawTimers->array[index], fallback.timers[index]);
        }
    }
    return result;
}

Settings settingsFromJson(const JsonValue& raw) {
    const auto* version = member(raw, "version");
    double versionNumber = 0;
    if (!jsonNumber(version, versionNumber) || versionNumber != 6) return defaultSettings();

    auto result = defaultSettings();
    double number = 0;
    if (jsonNumber(member(raw, "volume"), number)) result.volume = clampUnit(number, result.volume);
    if (member(raw, "systemToastsEnabled")) {
        result.systemToastsEnabled = jsonBool(member(raw, "systemToastsEnabled"), result.systemToastsEnabled);
    } else {
        result.systemToastsEnabled = jsonBool(member(raw, "toastEnabled"), result.systemToastsEnabled);
    }
    result.soundEnabled = jsonBool(member(raw, "soundEnabled"), result.soundEnabled);
    result.autoRefreshEnabled = jsonBool(member(raw, "autoRefreshEnabled"), result.autoRefreshEnabled);
    result.overlayWindowEnabled = jsonBool(member(raw, "overlayWindowEnabled"), result.overlayWindowEnabled);
    const auto* mode = member(raw, "overlayWindowMode");
    result.overlayWindowMode = mode && mode->kind == JsonValue::Kind::String && mode->string == "toast"
        ? OverlayWindowMode::Toast
        : OverlayWindowMode::Overview;

    const auto* rawOverlayCategories = member(raw, "overlayWindowCategories");
    if (rawOverlayCategories && rawOverlayCategories->kind == JsonValue::Kind::Object) {
        result.overlayWindowCategories[0] = jsonBool(member(*rawOverlayCategories, "helltide"), result.overlayWindowCategories[0]);
        result.overlayWindowCategories[1] = jsonBool(member(*rawOverlayCategories, "legion"), result.overlayWindowCategories[1]);
        result.overlayWindowCategories[2] = jsonBool(member(*rawOverlayCategories, "world_boss"), result.overlayWindowCategories[2]);
    }

    const auto rawColor = jsonString(member(raw, "overlayBgHex"), result.overlayBgHex);
    result.overlayBgHex = validHexColor(rawColor) ? lowerHex(rawColor) : result.overlayBgHex;
    if (jsonNumber(member(raw, "overlayScaleX"), number)) {
        result.overlayScaleX = clampFloat(number, result.overlayScaleX, 0.6, 2.0);
    } else if (jsonNumber(member(raw, "overlayScale"), number)) {
        result.overlayScaleX = clampFloat(number, result.overlayScaleX, 0.6, 2.0);
    }
    if (jsonNumber(member(raw, "overlayScaleY"), number)) {
        result.overlayScaleY = clampFloat(number, result.overlayScaleY, 0.6, 2.0);
    } else if (jsonNumber(member(raw, "overlayScale"), number)) {
        result.overlayScaleY = clampFloat(number, result.overlayScaleY, 0.6, 2.0);
    }
    if (jsonNumber(member(raw, "overlayBgOpacity"), number)) result.overlayBgOpacity = clampFloat(number, result.overlayBgOpacity, 0, 1.0);
    if (jsonNumber(member(raw, "overlayLineBgOpacity"), number)) result.overlayLineBgOpacity = clampFloat(number, result.overlayLineBgOpacity, 0, 1.0);

    const auto* rawCategories = member(raw, "categories");
    if (rawCategories && rawCategories->kind == JsonValue::Kind::Object) {
        result.categories[0] = normalizeCategory(member(*rawCategories, "helltide"), result.categories[0]);
        result.categories[1] = normalizeCategory(member(*rawCategories, "legion"), result.categories[1]);
        result.categories[2] = normalizeCategory(member(*rawCategories, "world_boss"), result.categories[2]);
    }
    return result;
}

Settings settingsFromJsonFile(const std::string& text) {
    JsonValue raw;
    JsonParser parser(text);
    return parser.parse(raw) ? settingsFromJson(raw) : defaultSettings();
}

std::wstring localAppData() {
    std::wstring buffer(32768, L'\0');
    const auto size = GetEnvironmentVariableW(L"LOCALAPPDATA", buffer.data(), static_cast<DWORD>(buffer.size()));
    if (size == 0 || size >= buffer.size()) return {};
    buffer.resize(size);
    return buffer;
}

std::wstring settingsDirectory() {
    const auto base = localAppData();
    return base.empty() ? std::wstring{} : base + L"\\HelltimeNative";
}

bool readFile(const std::wstring& path, std::string& output) {
    const auto handle = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (handle == INVALID_HANDLE_VALUE) return false;
    LARGE_INTEGER size{};
    const bool sizeOk = GetFileSizeEx(handle, &size);
    if (!sizeOk || size.QuadPart < 0 || static_cast<unsigned long long>(size.QuadPart) > kMaxJsonBytes) {
        CloseHandle(handle);
        return false;
    }
    output.assign(static_cast<std::size_t>(size.QuadPart), '\0');
    DWORD read = 0;
    const bool readOk = output.empty() || ReadFile(handle, output.data(), static_cast<DWORD>(output.size()), &read, nullptr) != FALSE;
    CloseHandle(handle);
    if (!readOk || read != output.size()) {
        output.clear();
        return false;
    }
    return true;
}

bool writeFile(const std::wstring& path, const std::string& content) {
    const auto directory = settingsDirectory();
    if (directory.empty()) return false;
    CreateDirectoryW(directory.c_str(), nullptr);
    const auto temporary = path + L".tmp";
    const auto handle = CreateFileW(temporary.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (handle == INVALID_HANDLE_VALUE) return false;
    DWORD written = 0;
    const bool writeOk = WriteFile(handle, content.data(), static_cast<DWORD>(content.size()), &written, nullptr) != FALSE && written == content.size();
    const bool flushOk = writeOk && FlushFileBuffers(handle) != FALSE;
    CloseHandle(handle);
    if (!writeOk || !flushOk || !MoveFileExW(temporary.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
        DeleteFileW(temporary.c_str());
        return false;
    }
    return true;
}

void appendJsonString(std::ostringstream& output, const std::string& value) {
    output << '"';
    for (const auto c : value) {
        switch (c) {
        case '"': output << "\\\""; break;
        case '\\': output << "\\\\"; break;
        case '\b': output << "\\b"; break;
        case '\f': output << "\\f"; break;
        case '\n': output << "\\n"; break;
        case '\r': output << "\\r"; break;
        case '\t': output << "\\t"; break;
        default:
            if (static_cast<unsigned char>(c) < 0x20) output << "\\u00" << std::hex << std::setw(2) << std::setfill('0') << static_cast<int>(static_cast<unsigned char>(c)) << std::dec;
            else output << c;
            break;
        }
    }
    output << '"';
}

void appendBool(std::ostringstream& output, bool value) { output << (value ? "true" : "false"); }

std::string settingsToJson(const Settings& settings) {
    const auto value = normalizeSettings(settings);
    std::ostringstream output;
    output << std::setprecision(17);
    output << "{\"version\":6,\"volume\":" << value.volume;
    output << ",\"systemToastsEnabled\":"; appendBool(output, value.systemToastsEnabled);
    output << ",\"soundEnabled\":"; appendBool(output, value.soundEnabled);
    output << ",\"autoRefreshEnabled\":"; appendBool(output, value.autoRefreshEnabled);
    output << ",\"overlayWindowEnabled\":"; appendBool(output, value.overlayWindowEnabled);
    output << ",\"overlayWindowMode\":"; appendJsonString(output, value.overlayWindowMode == OverlayWindowMode::Toast ? "toast" : "overview");
    output << ",\"overlayWindowCategories\":{\"helltide\":"; appendBool(output, value.overlayWindowCategories[0]);
    output << ",\"legion\":"; appendBool(output, value.overlayWindowCategories[1]);
    output << ",\"world_boss\":"; appendBool(output, value.overlayWindowCategories[2]);
    output << "},\"overlayBgHex\":"; appendJsonString(output, value.overlayBgHex);
    output << ",\"overlayScaleX\":" << value.overlayScaleX << ",\"overlayScaleY\":" << value.overlayScaleY;
    output << ",\"overlayBgOpacity\":" << value.overlayBgOpacity << ",\"overlayLineBgOpacity\":" << value.overlayLineBgOpacity;
    output << ",\"categories\":{";
    constexpr std::array<const char*, 3> names{"helltide", "legion", "world_boss"};
    for (std::size_t category = 0; category < value.categories.size(); ++category) {
        if (category != 0) output << ',';
        const auto& current = value.categories[category];
        output << '"' << names[category] << "\":{";
        output << "\"enabled\":"; appendBool(output, current.enabled);
        output << ",\"ttsName\":"; appendJsonString(output, current.ttsName);
        output << ",\"timerCount\":" << current.timerCount << ",\"timers\":[";
        for (std::size_t timer = 0; timer < current.timers.size(); ++timer) {
            if (timer != 0) output << ',';
            const auto& currentTimer = current.timers[timer];
            output << "{\"minutesBefore\":" << currentTimer.minutesBefore << ",\"ttsEnabled\":";
            appendBool(output, currentTimer.ttsEnabled);
            output << ",\"beepPattern\":"; appendJsonString(output, beepPatternToJson(currentTimer.beepPattern));
            output << ",\"pitchHz\":" << currentTimer.pitchHz << '}';
        }
        output << "]}";
    }
    output << "}}";
    return output.str();
}

} // namespace

Settings defaultSettings() {
    Settings result;
    result.categories = {};
    for (auto& category : result.categories) {
        category.enabled = true;
        category.ttsName = {};
        category.timerCount = 3;
        category.timers = {
            TimerSettings{30, true, BeepPattern::Beep, 880},
            TimerSettings{10, false, BeepPattern::Double, 880},
            TimerSettings{5, false, BeepPattern::Triple, 880},
        };
    }
    result.categories[0].ttsName = "H\xC3\xB6llenhochwasser";
    result.categories[1].ttsName = "Legionellen";
    result.categories[2].ttsName = "Weltscheff {boss}";
    return result;
}

Settings normalizeSettings(const Settings& settings) {
    auto result = defaultSettings();
    result.volume = clampUnit(settings.volume, result.volume);
    result.systemToastsEnabled = settings.systemToastsEnabled;
    result.soundEnabled = settings.soundEnabled;
    result.autoRefreshEnabled = settings.autoRefreshEnabled;
    result.overlayWindowEnabled = settings.overlayWindowEnabled;
    result.overlayWindowMode = settings.overlayWindowMode == OverlayWindowMode::Toast ? OverlayWindowMode::Toast : OverlayWindowMode::Overview;
    result.overlayWindowCategories = settings.overlayWindowCategories;
    result.overlayBgHex = validHexColor(settings.overlayBgHex) ? lowerHex(settings.overlayBgHex) : result.overlayBgHex;
    result.overlayScaleX = clampFloat(settings.overlayScaleX, result.overlayScaleX, 0.6, 2.0);
    result.overlayScaleY = clampFloat(settings.overlayScaleY, result.overlayScaleY, 0.6, 2.0);
    result.overlayBgOpacity = clampFloat(settings.overlayBgOpacity, result.overlayBgOpacity, 0, 1.0);
    result.overlayLineBgOpacity = clampFloat(settings.overlayLineBgOpacity, result.overlayLineBgOpacity, 0, 1.0);
    for (std::size_t category = 0; category < result.categories.size(); ++category) {
        result.categories[category].enabled = settings.categories[category].enabled;
        result.categories[category].ttsName = trimAndLimit(settings.categories[category].ttsName);
        result.categories[category].timerCount = settings.categories[category].timerCount >= 1 && settings.categories[category].timerCount <= 3
            ? settings.categories[category].timerCount
            : result.categories[category].timerCount;
        for (std::size_t timer = 0; timer < result.categories[category].timers.size(); ++timer) {
            const auto& source = settings.categories[category].timers[timer];
            auto& destination = result.categories[category].timers[timer];
            destination.minutesBefore = std::clamp((std::max(source.minutesBefore, 0) + 2) / 5 * 5, 0, 60);
            destination.ttsEnabled = source.ttsEnabled;
            destination.beepPattern = source.beepPattern;
            destination.pitchHz = std::clamp((std::max(source.pitchHz, 200) - 200 + 50) / 100 * 100 + 200, 200, 2000);
        }
    }
    return result;
}

std::wstring settingsFilePath() {
    const auto directory = settingsDirectory();
    return directory.empty() ? std::wstring{} : directory + L"\\settings.json";
}

Settings loadSettings() {
    const auto path = settingsFilePath();
    std::string content;
    auto result = path.empty() || !readFile(path, content) ? defaultSettings() : settingsFromJsonFile(content);
    result = normalizeSettings(result);
    if (isPanicStopEnabled()) {
        result.overlayWindowEnabled = false;
        result.soundEnabled = false;
        result.autoRefreshEnabled = false;
    }
    return result;
}

bool saveSettings(const Settings& settings) {
    const auto path = settingsFilePath();
    return !path.empty() && writeFile(path, settingsToJson(settings));
}

} // namespace helltime::domain
