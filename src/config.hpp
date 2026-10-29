#pragma once
#include "config_types.hpp"
#include "keys.hpp"
#include <X11/XF86keysym.h>
#include <X11/keysym.h>
#include <array>
#include <chrono>
#include <string_view>
namespace ppwm::config {
inline constexpr unsigned MOD = Mod1Mask; // Alt; use Mod4Mask for Super.
inline constexpr std::array<std::string_view, 5> terminal_classes{"urxvt", "xterm", "st",
                                                                  "alacritty", "kitty"};
inline constexpr bool swallow_automatic = true;
inline constexpr int bar_height = 24, border = 2, gap = 0, tag_width = 32, snap = 20;
inline constexpr int tray_icon_size = 20, tray_spacing = 2;
inline constexpr int layout_width = 50, text_padding = 7;
inline constexpr int default_master_count = 1;
inline constexpr std::size_t default_layout = 0, default_previous_layout = 1;
inline constexpr bool default_bar_visible = true, focus_follows_mouse = true, warp_pointer = true;
inline constexpr std::array<std::string_view, 9> tag_labels{"1", "2", "3", "4", "5",
                                                            "6", "7", "8", "9"};
inline constexpr char clock_format[] = "%H:%M";
inline constexpr bool topbar = false, view_on_tag = true;
inline constexpr double master_ratio = 0.55, ratio_step = 0.03;
inline constexpr auto bar_interval = std::chrono::milliseconds(50);
inline constexpr auto status_interval = std::chrono::milliseconds(2000);
inline constexpr char font[] =
    "Lilex Nerd Font Mono:style=Regular:pixelsize=14:antialias=true:hinting=true";
inline constexpr char background[] = "#000000", foreground[] = "#a0a0a0", accent[] = "#202020";
inline constexpr char bar_muted[] = "#686868", bar_bright[] = "#e0e0e0";
inline constexpr char bar_title_background[] = "#101010", bar_divider[] = "#353535";
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
inline constexpr Binding keys[]{
    {MOD, XK_1, Action::ViewTag, 1},
    {MOD | ControlMask, XK_1, Action::ToggleViewTag, 1},
    {MOD | ShiftMask, XK_1, Action::SendTag, 1},
    {MOD | ShiftMask | ControlMask, XK_1, Action::ToggleSendTag, 1},
    {MOD, XK_2, Action::ViewTag, 2},
    {MOD | ControlMask, XK_2, Action::ToggleViewTag, 2},
    {MOD | ShiftMask, XK_2, Action::SendTag, 2},
    {MOD | ShiftMask | ControlMask, XK_2, Action::ToggleSendTag, 2},
    {MOD, XK_3, Action::ViewTag, 3},
    {MOD | ControlMask, XK_3, Action::ToggleViewTag, 3},
    {MOD | ShiftMask, XK_3, Action::SendTag, 3},
    {MOD | ShiftMask | ControlMask, XK_3, Action::ToggleSendTag, 3},
    {MOD, XK_4, Action::ViewTag, 4},
    {MOD | ControlMask, XK_4, Action::ToggleViewTag, 4},
    {MOD | ShiftMask, XK_4, Action::SendTag, 4},
    {MOD | ShiftMask | ControlMask, XK_4, Action::ToggleSendTag, 4},
    {MOD, XK_5, Action::ViewTag, 5},
    {MOD | ControlMask, XK_5, Action::ToggleViewTag, 5},
    {MOD | ShiftMask, XK_5, Action::SendTag, 5},
    {MOD | ShiftMask | ControlMask, XK_5, Action::ToggleSendTag, 5},
    {MOD, XK_6, Action::ViewTag, 6},
    {MOD | ControlMask, XK_6, Action::ToggleViewTag, 6},
    {MOD | ShiftMask, XK_6, Action::SendTag, 6},
    {MOD | ShiftMask | ControlMask, XK_6, Action::ToggleSendTag, 6},
    {MOD, XK_7, Action::ViewTag, 7},
    {MOD | ControlMask, XK_7, Action::ToggleViewTag, 7},
    {MOD | ShiftMask, XK_7, Action::SendTag, 7},
    {MOD | ShiftMask | ControlMask, XK_7, Action::ToggleSendTag, 7},
    {MOD, XK_8, Action::ViewTag, 8},
    {MOD | ControlMask, XK_8, Action::ToggleViewTag, 8},
    {MOD | ShiftMask, XK_8, Action::SendTag, 8},
    {MOD | ShiftMask | ControlMask, XK_8, Action::ToggleSendTag, 8},
    {MOD, XK_9, Action::ViewTag, 9},
    {MOD | ControlMask, XK_9, Action::ToggleViewTag, 9},
    {MOD | ShiftMask, XK_9, Action::SendTag, 9},
    {MOD | ShiftMask | ControlMask, XK_9, Action::ToggleSendTag, 9},
    {MOD, XK_a, Action::Spawn, 0, terminal},
    {MOD, XK_p, Action::Spawn, 0, launcher},
    {MOD, XK_s, Action::Spawn, 0, "\"$HOME/.local/bin/browser\""},
    {MOD, XK_e, Action::Spawn, 0, "\"$HOME/.local/bin/file-manager-tui\""},
    {MOD, XK_d, Action::Spawn, 0, "\"$HOME/.local/bin/pdf.sh\""},
    {MOD | ShiftMask, XK_p, Action::Spawn, 0, "\"$HOME/.local/bin/pdf.sh\""},
    {MOD, XK_w, Action::Spawn, 0, "/usr/sbin/pavucontrol"},
    {MOD, XK_r, Action::Spawn, 0, "\"$HOME/.local/bin/task-manager\""},
    {MOD, XK_Escape, Action::Spawn, 0, "\"$HOME/.local/bin/task-manager\""},
    {MOD, XK_F12, Action::Spawn, 0, "\"$HOME/.local/bin/keep-awake\" toggle"},
    {MOD, XK_y, Action::Spawn, 0, "networkmanager_dmenu"},
    {MOD, XK_n, Action::Spawn, 0, "urxvtc +j -e nvim || urxvt +j -e nvim"},
    {MOD, XK_quotedbl, Action::Spawn, 0, "\"$HOME/.local/bin/system-menu.sh\""},
    {MOD | ShiftMask, XK_u, Action::Spawn, 0, "jgmenu_run > ~/.cache/jgmenu/menu.csv"},
    {MOD | ShiftMask, XK_c, Action::Spawn, 0, "/usr/local/bin/capturecolor.sh"},
    {MOD | ShiftMask, XK_f, Action::Spawn, 0,
     "urxvtc +j -e \"$HOME/.local/bin/file-search\" || urxvt +j -e "
     "\"$HOME/.local/bin/file-search\""},
    {0, XK_Print, Action::Spawn, 0, "QT_QPA_PLATFORM=xcb flameshot gui"},
    {MOD, XK_Print, Action::Spawn, 0, "flameshot full -p ~/Pictures/Screenshots"},
    {0, XF86XK_MonBrightnessUp, Action::Spawn, 0, "brightnessctl set 5%+"},
    {0, XF86XK_MonBrightnessDown, Action::Spawn, 0, "brightnessctl set 5%-"},
    {0, XF86XK_AudioLowerVolume, Action::Spawn, 0, "/usr/local/bin/skontrol down"},
    {0, XF86XK_AudioRaiseVolume, Action::Spawn, 0, "/usr/local/bin/skontrol up"},
    {0, XF86XK_AudioMute, Action::Spawn, 0, "/usr/local/bin/skontrol mute"},
    {MOD, XK_j, Action::Focus, 1},
    {MOD | ShiftMask, XK_j, Action::FocusHidden, 1},
    {MOD | ShiftMask, XK_k, Action::FocusHidden, -1},
    {MOD, XK_Left, Action::MasterCount, 1},
    {MOD, XK_Right, Action::MasterCount, -1},
    {MOD, XK_0, Action::AllTags},
    {MOD | ShiftMask, XK_0, Action::TagAll},
    {MOD, XK_comma, Action::FocusMonitor, -1},
    {MOD, XK_period, Action::FocusMonitor, 1},
    {MOD | ShiftMask, XK_comma, Action::SendMonitor, -1},
    {MOD | ShiftMask, XK_period, Action::SendMonitor, 1},
    {MOD | ShiftMask | ControlMask, XK_q, Action::Restart},
    {MOD, XK_k, Action::Focus, -1},
    {MOD, XK_l, Action::Ratio, -1},
    {MOD, XK_i, Action::Ratio, 1},
    {MOD, XK_t, Action::Layout, 0},
    {MOD, XK_f, Action::Layout, 1},
    {MOD, XK_m, Action::Layout, 2},
    {MOD, XK_space, Action::CycleLayout},
    {MOD | ShiftMask, XK_space, Action::Float},
    {MOD, XK_F11, Action::Fullscreen},
    {MOD, XK_Return, Action::Zoom},
    {MOD, XK_Tab, Action::PreviousTag},
    {MOD, XK_b, Action::Bar},
    {MOD, XK_h, Action::Hide},
    {MOD | ShiftMask, XK_s, Action::ShowAll},
    {MOD, XK_grave, Action::Scratch},
    {MOD | ShiftMask, XK_Down, Action::Push, 1},
    {MOD | ShiftMask, XK_Up, Action::Push, -1},
    {MOD, XK_q, Action::Close},
    {MOD | ShiftMask, XK_q, Action::Quit}};

// Tag key arguments are 1..9. Mouse tag arguments come from the clicked label.
inline constexpr MouseBinding mouse_bindings[]{
    {MouseContext::Client, MOD, Button1, MouseAction::Move},
    {MouseContext::Client, MOD, Button2, MouseAction::Float},
    {MouseContext::Client, MOD, Button3, MouseAction::Resize},
    {MouseContext::Tag, 0, Button1, MouseAction::ViewTag},
    {MouseContext::Tag, 0, Button3, MouseAction::ToggleViewTag},
    {MouseContext::Tag, MOD, Button1, MouseAction::SendTag},
    {MouseContext::Tag, MOD, Button3, MouseAction::ToggleSendTag},
    {MouseContext::Layout, 0, Button1, MouseAction::CycleLayout},
    {MouseContext::Layout, 0, Button3, MouseAction::Layout, 2},
    {MouseContext::Title, 0, Button1, MouseAction::ToggleHide},
    {MouseContext::Title, 0, Button2, MouseAction::FocusZoom},
    {MouseContext::StatusArea, 0, Button1, MouseAction::ModuleCommand},
    {MouseContext::StatusArea, 0, Button3, MouseAction::ModuleCommand},
    {MouseContext::StatusArea, 0, Button2, MouseAction::Spawn, 0, terminal}};
// Fast local sources only; the bar timer never invokes shell commands.
inline constexpr std::array<BarModule, 3> status_modules{
    {{bar_modules::battery,
      "urxvtc +j -e watch -n 5 upower -i /org/freedesktop/UPower/devices/battery_BAT0"},
     {bar_modules::clock_module,
      "urxvtc +j -e sh -c 'cal -m; printf \"\\nKapatmak icin Enter\\n\"; read _'"},
     {bar_modules::keep_awake, nullptr}}};
static_assert(tag_width > 0 && layout_width > 0 && text_padding >= 0);
static_assert(default_master_count >= 0);
} // namespace ppwm::config
