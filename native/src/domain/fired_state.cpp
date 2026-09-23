#include "fired_state.h"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <algorithm>
#include <charconv>
#include <cstddef>
#include <limits>
#include <string>
#include <string_view>

namespace helltime::domain {
namespace {

constexpr std::size_t kMaxJsonBytes = 64 * 1024;
constexpr std::size_t kMaxEntries = 4096;

std::wstring localAppData() {
    std::wstring buffer(32768, L'\0');
    const auto size = GetEnvironmentVariableW(
        L"LOCALAPPDATA", buffer.data(), static_cast<DWORD>(buffer.size()));
    if (size == 0 || size >= buffer.size()) return {};
    buffer.resize(size);
    return buffer;
}

std::wstring appDataDirectory() {
    const auto base = localAppData();
    return base.empty() ? std::wstring{} : base + L"\\HelltimeNative";
}

bool readFile(const std::wstring& path, std::string& output) {
    const auto handle = CreateFileW(
        path.c_str(),
        GENERIC_READ,
        FILE_SHARE_READ,
        nullptr,
        OPEN_EXISTING,
        FILE_ATTRIBUTE_NORMAL,
        nullptr);
    if (handle == INVALID_HANDLE_VALUE) return false;

    LARGE_INTEGER size{};
    const bool sizeOk = GetFileSizeEx(handle, &size);
    if (!sizeOk || size.QuadPart < 0 ||
        static_cast<unsigned long long>(size.QuadPart) > kMaxJsonBytes) {
        CloseHandle(handle);
        return false;
    }

    output.assign(static_cast<std::size_t>(size.QuadPart), '\0');
    DWORD read = 0;
    const bool readOk = output.empty() ||
        ReadFile(handle, output.data(), static_cast<DWORD>(output.size()), &read, nullptr) != FALSE;
    CloseHandle(handle);
    if (!readOk || read != output.size()) {
        output.clear();
        return false;
    }
    return true;
}

bool writeFile(const std::wstring& path, const std::string& content) {
    const auto directory = appDataDirectory();
    if (directory.empty()) return false;
    CreateDirectoryW(directory.c_str(), nullptr);

    const auto temporary = path + L".tmp";
    const auto handle = CreateFileW(
        temporary.c_str(),
        GENERIC_WRITE,
        0,
        nullptr,
        CREATE_ALWAYS,
        FILE_ATTRIBUTE_NORMAL,
        nullptr);
    if (handle == INVALID_HANDLE_VALUE) return false;

    DWORD written = 0;
    const bool writeOk = WriteFile(
        handle,
        content.data(),
        static_cast<DWORD>(content.size()),
        &written,
        nullptr) != FALSE && written == content.size();
    const bool flushOk = writeOk && FlushFileBuffers(handle) != FALSE;
    CloseHandle(handle);

    if (!writeOk || !flushOk ||
        !MoveFileExW(
            temporary.c_str(),
            path.c_str(),
            MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
        DeleteFileW(temporary.c_str());
        return false;
    }
    return true;
}

class JsonParser {
public:
    explicit JsonParser(std::string_view input) : input_(input) {}

    bool parse(FiredState& output) {
        skipWhitespace();
        if (!consume('{')) return false;
        skipWhitespace();
        if (consume('}')) return finish(output);

        FiredState parsed;
        for (std::size_t count = 0; count < kMaxEntries; ++count) {
            skipWhitespace();
            std::string key;
            if (!parseString(key) || key.empty()) return false;
            skipWhitespace();
            if (!consume(':')) return false;
            skipWhitespace();
            std::int64_t timestamp = 0;
            if (!parseInteger(timestamp)) return false;
            if (!parsed.emplace(std::move(key), timestamp).second) return false;
            skipWhitespace();
            if (consume('}')) {
                output = std::move(parsed);
                return finish(output);
            }
            if (!consume(',')) return false;
        }
        return false;
    }

private:
    std::string_view input_;
    std::size_t position_{};

    bool finish(const FiredState&) {
        skipWhitespace();
        return position_ == input_.size();
    }

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
        } else if (codepoint <= 0x10FFFF) {
            output.push_back(static_cast<char>(0xF0 | (codepoint >> 18)));
            output.push_back(static_cast<char>(0x80 | ((codepoint >> 12) & 0x3F)));
            output.push_back(static_cast<char>(0x80 | ((codepoint >> 6) & 0x3F)));
            output.push_back(static_cast<char>(0x80 | (codepoint & 0x3F)));
        }
    }

    bool parseUnicodeEscape(std::string& output) {
        if (position_ + 4 > input_.size()) return false;
        unsigned int codepoint = 0;
        for (int index = 0; index < 4; ++index) {
            unsigned int digit = 0;
            if (!hexDigit(input_[position_++], digit)) return false;
            codepoint = (codepoint << 4) | digit;
        }

        if (codepoint >= 0xD800 && codepoint <= 0xDBFF) {
            if (position_ + 6 > input_.size() || input_[position_] != '\\' || input_[position_ + 1] != 'u') {
                return false;
            }
            position_ += 2;
            unsigned int low = 0;
            for (int index = 0; index < 4; ++index) {
                unsigned int digit = 0;
                if (!hexDigit(input_[position_++], digit)) return false;
                low = (low << 4) | digit;
            }
            if (low < 0xDC00 || low > 0xDFFF) return false;
            codepoint = 0x10000 + ((codepoint - 0xD800) << 10) + (low - 0xDC00);
        } else if (codepoint >= 0xDC00 && codepoint <= 0xDFFF) {
            return false;
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
            if (output.size() > kMaxJsonBytes) return false;
        }
        return false;
    }

    bool parseInteger(std::int64_t& output) {
        const auto start = position_;
        if (position_ < input_.size() && input_[position_] == '-') ++position_;
        if (position_ >= input_.size()) return false;
        if (input_[position_] == '0') {
            ++position_;
        } else {
            if (input_[position_] < '1' || input_[position_] > '9') return false;
            while (position_ < input_.size() && input_[position_] >= '0' && input_[position_] <= '9') ++position_;
        }
        const auto token = input_.substr(start, position_ - start);
        const auto result = std::from_chars(token.data(), token.data() + token.size(), output);
        return result.ec == std::errc{} && result.ptr == token.data() + token.size();
    }
};

void appendJsonString(std::string& output, std::string_view value) {
    constexpr char kHex[] = "0123456789abcdef";
    output.push_back('"');
    for (const auto c : value) {
        switch (c) {
        case '"': output += "\\\""; break;
        case '\\': output += "\\\\"; break;
        case '\b': output += "\\b"; break;
        case '\f': output += "\\f"; break;
        case '\n': output += "\\n"; break;
        case '\r': output += "\\r"; break;
        case '\t': output += "\\t"; break;
        default:
            if (static_cast<unsigned char>(c) < 0x20) {
                const auto byte = static_cast<unsigned char>(c);
                output += "\\u00";
                output.push_back(kHex[byte >> 4]);
                output.push_back(kHex[byte & 0x0F]);
            } else {
                output.push_back(c);
            }
            break;
        }
    }
    output.push_back('"');
}

std::string toJson(const FiredState& state) {
    std::string output;
    output.reserve(std::min<std::size_t>(state.size() * 48 + 2, kMaxJsonBytes));
    output.push_back('{');
    bool first = true;
    for (const auto& [key, timestamp] : state) {
        if (!first) output.push_back(',');
        first = false;
        appendJsonString(output, key);
        output.push_back(':');
        output += std::to_string(timestamp);
    }
    output.push_back('}');
    return output;
}

bool isFreshTimestamp(std::int64_t timestamp, std::int64_t nowMs) {
    const auto cutoff = nowMs > std::numeric_limits<std::int64_t>::min() + FIRED_STATE_RETENTION_MS
        ? nowMs - FIRED_STATE_RETENTION_MS
        : std::numeric_limits<std::int64_t>::min();
    // Deliberately no upper bound: this matches App.tsx pruneFired and keeps
    // clock-skewed/future marks until they age past the lower cutoff.
    return timestamp >= cutoff;
}

} // namespace

std::wstring firedStateFilePath() {
    const auto directory = appDataDirectory();
    return directory.empty() ? std::wstring{} : directory + L"\\fired.json";
}

FiredState loadFiredState() {
    const auto path = firedStateFilePath();
    std::string content;
    if (path.empty() || !readFile(path, content)) return {};
    FiredState result;
    return JsonParser(content).parse(result) ? result : FiredState{};
}

bool saveFiredState(const FiredState& state) {
    const auto path = firedStateFilePath();
    if (path.empty()) return false;
    const auto content = toJson(state);
    return content.size() <= kMaxJsonBytes && writeFile(path, content);
}

void pruneFiredState(FiredState& state, std::int64_t nowMs) {
    for (auto it = state.begin(); it != state.end();) {
        if (!isFreshTimestamp(it->second, nowMs)) it = state.erase(it);
        else ++it;
    }
}

bool hasFreshFired(const FiredState& state, std::string_view key, std::int64_t nowMs) {
    const auto it = state.find(std::string(key));
    return it != state.end() && isFreshTimestamp(it->second, nowMs);
}

bool markFiredIfFresh(FiredState& state, std::string_view key, std::int64_t nowMs) {
    if (key.empty() || hasFreshFired(state, key, nowMs)) return false;
    state[std::string(key)] = nowMs;
    return true;
}

} // namespace helltime::domain
