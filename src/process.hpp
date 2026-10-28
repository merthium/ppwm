#pragma once

#include <X11/Xlib.h>
#include <sys/types.h>
#include <optional>
#include <string_view>

namespace ppwm {
namespace detail {
std::optional<Window> parse_window_id(std::string_view value);
}

class ProcessLookup {
public:
    explicit ProcessLookup(Display* display);
    std::optional<pid_t> window_pid(Window window) const;
    std::optional<Window> terminal_window(pid_t pid) const;

private:
    Display* display_;
    bool xres_supported_ = false;
};
} // namespace ppwm
