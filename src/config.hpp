#pragma once
#include <X11/Xlib.h>
#include <array>
#include <chrono>
#include <string_view>
namespace ppwm::config {
inline constexpr std::array<std::string_view, 5> terminal_classes{"urxvt", "xterm", "st",
                                                                  "alacritty", "kitty"};
inline constexpr bool swallow_automatic = true;
inline constexpr unsigned modifier = Mod1Mask;
inline constexpr int bar_height = 24, border = 2, gap = 0, tag_width = 32, snap = 20;
inline constexpr int tray_icon_size = 20, tray_spacing = 2;
inline constexpr bool topbar = false, view_on_tag = true;
inline constexpr double master_ratio = 0.55, ratio_step = 0.03;
inline constexpr auto bar_interval = std::chrono::milliseconds(2000);
inline constexpr char font[] =
    "Fragment Mono:style=Regular:size=14:antialias=true:hinting=true:embolden=true";
inline constexpr char background[] = "#000000", foreground[] = "#a0a0a0", accent[] = "#202020";
inline constexpr char normal_border[] = "#202020", selected_border[] = "#808080";
inline constexpr char terminal[] = "urxvtc +j || urxvt +j";
inline constexpr char launcher[] =
    "dmenu_run -m 0 -nb '#000000' -nf '#a0a0a0' -sb '#202020' -sf '#a0a0a0' -p '>>>'";
inline constexpr char scratchpad[] =
    "urxvtc +j -name scratchpad -geometry 100x28 || urxvt +j -name scratchpad -geometry 100x28";
inline constexpr char battery_path[] = "/sys/class/power_supply/BAT0/capacity";
inline constexpr char awake_lock[] = ".cache/keep-awake/running.lock";
struct Rule {
    const char *class_name;
    const char *instance;
    bool floating;
    unsigned tag; // 0=current, 1..9=destination
    bool scratchpad;
};
inline constexpr std::array rules{
    Rule{"Emacs", nullptr, false, 2, false}, Rule{"popup", nullptr, true, 0, false},
    Rule{"Blueman-manager", nullptr, true, 0, false}, Rule{"URxvt", "scratchpad", true, 0, true}};
} // namespace ppwm::config
