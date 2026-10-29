#pragma once
#include <string>
namespace ppwm {
struct BarState;
struct BarModule {
    std::string (*read)(const BarState &);
    const char *on_click{};
};
namespace bar_modules {
std::string battery(const BarState &);
std::string clock_module(const BarState &);
std::string keep_awake(const BarState &);
} // namespace bar_modules
} // namespace ppwm
