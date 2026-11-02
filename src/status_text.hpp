#pragma once
#include <algorithm>
#include <charconv>
#include <string>
#include <string_view>
#include <vector>
namespace ppwm {
enum class StatusKind { Text, Foreground, Background, Reset, Offset, Rectangle };
struct StatusPart {
    StatusKind kind;
    std::string text;
    int x{}, y{}, width{}, height{};
};
inline std::vector<StatusPart> parse_status(std::string_view input) {
    input = input.substr(0, 4096);
    std::vector<StatusPart> parts;
    auto text = [&](char c) {
        if (static_cast<unsigned char>(c) < 32 || c == 127)
            return;
        if (parts.empty() || parts.back().kind != StatusKind::Text)
            parts.push_back({StatusKind::Text, {}});
        parts.back().text += c;
    };
    for (std::size_t i = 0; i < input.size();) {
        if (input[i] != '^') {
            text(input[i++]);
            continue;
        }
        const auto end = input.find('^', i + 1);
        if (end == std::string_view::npos) {
            text(input[i++]);
            continue;
        }
        if (end == i + 1) {
            text('^');
            i = end + 1;
            continue;
        }
        auto command = input.substr(i + 1, end - i - 1);
        std::size_t at = 0;
        auto number = [&](int &value) {
            const auto *start = command.data() + at;
            const auto result = std::from_chars(start, command.data() + command.size(), value);
            if (result.ec != std::errc{})
                return false;
            at = static_cast<std::size_t>(result.ptr - command.data());
            value = std::clamp(value, -4096, 4096);
            return true;
        };
        while (at < command.size()) {
            const char op = command[at++];
            if (op == 'd')
                parts.push_back({StatusKind::Reset, {}});
            else if ((op == 'c' || op == 'b') && command.size() - at >= 7 && command[at] == '#') {
                const auto color = command.substr(at, 7);
                const bool valid = std::all_of(color.begin() + 1, color.end(), [](char c) {
                    return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') ||
                           (c >= 'A' && c <= 'F');
                });
                if (valid)
                    parts.push_back({op == 'c' ? StatusKind::Foreground : StatusKind::Background,
                                     std::string(color)});
                at += 7;
            } else if (op == 'f') {
                int width{};
                if (number(width))
                    parts.push_back({StatusKind::Offset, {}, width});
            } else if (op == 'r') {
                int values[4]{};
                bool valid = true;
                for (int n = 0; n < 4; ++n) {
                    if (!number(values[n])) {
                        valid = false;
                        break;
                    }
                    if (n < 3 && (at == command.size() || command[at++] != ',')) {
                        valid = false;
                        break;
                    }
                }
                if (valid && values[2] > 0 && values[3] > 0)
                    parts.push_back(
                        {StatusKind::Rectangle, {}, values[0], values[1], values[2], values[3]});
            }
        }
        i = end + 1;
    }
    return parts;
}
} // namespace ppwm
