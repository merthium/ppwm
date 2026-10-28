#include "process.hpp"

#include <X11/extensions/XRes.h>
#include <charconv>
#include <cstdint>
#include <fstream>
#include <memory>
#include <string>

namespace ppwm {
namespace detail {
std::optional<Window> parse_window_id(std::string_view value) {
    int base = 10;
    if (value.starts_with("0x") || value.starts_with("0X")) {
        value.remove_prefix(2);
        base = 16;
    }
    if (value.empty())
        return std::nullopt;
    std::uint32_t id = 0;
    const auto [end, error] = std::from_chars(value.data(), value.data() + value.size(), id, base);
    if (error != std::errc{} || end != value.data() + value.size() || id == 0)
        return std::nullopt;
    return static_cast<Window>(id);
}
} // namespace detail

namespace {
std::optional<Window> environment_window(pid_t pid) {
    std::ifstream environment("/proc/" + std::to_string(pid) + "/environ", std::ios::binary);
    // Bound both total work and temporary storage, even for very large environments.
    constexpr std::size_t max_bytes = 1024 * 1024;
    constexpr std::size_t max_entry = 128;
    std::string entry;
    bool truncated = false;
    char byte = 0;
    for (std::size_t scanned = 0; scanned < max_bytes && environment.get(byte); ++scanned) {
        if (byte != '\0') {
            if (entry.size() < max_entry)
                entry.push_back(byte);
            else
                truncated = true;
            continue;
        }
        if (!truncated && entry.starts_with("WINDOWID="))
            return detail::parse_window_id(std::string_view(entry).substr(9));
        entry.clear();
        truncated = false;
    }
    return std::nullopt;
}

std::optional<pid_t> parent_pid(pid_t pid) {
    std::ifstream status("/proc/" + std::to_string(pid) + "/status");
    std::string line;
    for (int lines = 0; lines < 128 && std::getline(status, line); ++lines) {
        if (!line.starts_with("PPid:"))
            continue;
        std::string_view value(line);
        value.remove_prefix(5);
        const auto first = value.find_first_not_of(" \t");
        if (first == std::string_view::npos)
            return std::nullopt;
        value.remove_prefix(first);
        pid_t parent = 0;
        const auto [end, error] = std::from_chars(value.data(), value.data() + value.size(), parent);
        if (error == std::errc{} && end == value.data() + value.size() && parent > 0)
            return parent;
        return std::nullopt;
    }
    return std::nullopt;
}
} // namespace

ProcessLookup::ProcessLookup(Display* display) : display_(display) {
    int event_base = 0;
    int error_base = 0;
    int major = 0;
    int minor = 0;
    xres_supported_ = display_ && XResQueryExtension(display_, &event_base, &error_base) &&
                      XResQueryVersion(display_, &major, &minor) &&
                      (major > 1 || (major == 1 && minor >= 2));
}

std::optional<pid_t> ProcessLookup::window_pid(Window window) const {
    if (!xres_supported_ || window == None)
        return std::nullopt;
    XResClientIdSpec spec{window, XRES_CLIENT_ID_PID_MASK};
    long count = 0;
    XResClientIdValue* values = nullptr;
    const auto result = XResQueryClientIds(display_, 1, &spec, &count, &values);
    const auto destroy = [count](XResClientIdValue* ids) { XResClientIdsDestroy(count, ids); };
    const std::unique_ptr<XResClientIdValue, decltype(destroy)> owner(values, destroy);
    // Unlike XResQueryVersion, QueryClientIds returns the X11 Success code (zero).
    if (result != Success || !values)
        return std::nullopt;
    for (long index = 0; index < count; ++index) {
        if (XResGetClientIdType(&values[index]) != XRES_CLIENT_ID_PID)
            continue;
        const pid_t pid = XResGetClientPid(&values[index]);
        if (pid > 0)
            return pid;
    }
    return std::nullopt;
}

std::optional<Window> ProcessLookup::terminal_window(pid_t pid) const {
    for (int depth = 0; depth < 64 && pid > 0; ++depth) {
        if (auto window = environment_window(pid))
            return window;
        const auto parent = parent_pid(pid);
        if (!parent || *parent == pid)
            break;
        pid = *parent;
    }
    return std::nullopt;
}
} // namespace ppwm
