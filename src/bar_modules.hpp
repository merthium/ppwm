#pragma once
#include "bar.hpp"
#include "config.hpp"
#include <array>
#include <cerrno>
#include <cstdlib>
#include <ctime>
#include <fcntl.h>
#include <fstream>
#include <sys/file.h>
#include <unistd.h>
namespace ppwm::bar_modules {
inline std::string battery(const BarState &) {
    std::ifstream file(config::battery_path);
    int percent = 0;
    return file >> percent ? "BAT " + std::to_string(percent) + "%" : "BAT n/a";
}
inline std::string clock_module(const BarState &) {
    char b[40];
    auto now = std::time(nullptr);
    std::tm t{};
    localtime_r(&now, &t);
    std::strftime(b, sizeof b, "%H:%M", &t);
    return b;
}
inline std::string keep_awake(const BarState &) {
    const char *home = std::getenv("HOME");
    if (!home)
        return "AWAKE ?";
    const auto lock_path = std::string(home) + "/" + config::awake_lock;
    int fd = open(lock_path.c_str(), O_RDONLY | O_CLOEXEC);
    if (fd < 0)
        return errno == ENOENT ? "AWAKE OFF" : "AWAKE ?";
    int result = flock(fd, LOCK_EX | LOCK_NB), error = errno;
    close(fd);
    return result == 0 ? "AWAKE OFF" : error == EWOULDBLOCK ? "AWAKE ON" : "AWAKE ?";
}
// Fast local sources only; the bar timer never invokes shell commands.
inline constexpr std::array<BarModule, 3> modules{
    {{battery, "urxvtc +j -e watch -n 5 upower -i /org/freedesktop/UPower/devices/battery_BAT0"},
     {clock_module, "urxvtc +j -e sh -c 'cal -m; printf \"\\nKapatmak icin Enter\\n\"; read _'"},
     {keep_awake, nullptr}}};
} // namespace ppwm::bar_modules
