#pragma once
#include "config.hpp"
#include <X11/XF86keysym.h>
#include <X11/keysym.h>
namespace ppwm::config {
enum class Action {
    Spawn,
    Focus,
    FocusHidden,
    MasterCount,
    AllTags,
    TagAll,
    Restart,
    FocusMonitor,
    SendMonitor,
    Ratio,
    Layout,
    CycleLayout,
    Float,
    Fullscreen,
    Close,
    Quit,
    Zoom,
    PreviousTag,
    Bar,
    Hide,
    ShowAll,
    Scratch,
    Push
};
struct Binding {
    unsigned modifiers;
    KeySym key;
    Action action;
    int argument{};
    const char *command{};
};
inline constexpr unsigned alt = modifier, shift = modifier | ShiftMask;
inline constexpr Binding keys[]{
    {alt, XK_a, Action::Spawn, 0, terminal},
    {alt, XK_p, Action::Spawn, 0, launcher},
    {alt, XK_s, Action::Spawn, 0, "\"$HOME/.local/bin/browser\""},
    {alt, XK_e, Action::Spawn, 0, "\"$HOME/.local/bin/file-manager-tui\""},
    {alt, XK_d, Action::Spawn, 0, "\"$HOME/.local/bin/pdf.sh\""},
    {shift, XK_p, Action::Spawn, 0, "\"$HOME/.local/bin/pdf.sh\""},
    {alt, XK_w, Action::Spawn, 0, "/usr/sbin/pavucontrol"},
    {alt, XK_r, Action::Spawn, 0, "\"$HOME/.local/bin/task-manager\""},
    {alt, XK_Escape, Action::Spawn, 0, "\"$HOME/.local/bin/task-manager\""},
    {alt, XK_F12, Action::Spawn, 0, "\"$HOME/.local/bin/keep-awake\" toggle"},
    {alt, XK_y, Action::Spawn, 0, "networkmanager_dmenu"},
    {alt, XK_n, Action::Spawn, 0, "urxvtc +j -e nvim || urxvt +j -e nvim"},
    {alt, XK_quotedbl, Action::Spawn, 0, "\"$HOME/.local/bin/system-menu.sh\""},
    {shift, XK_u, Action::Spawn, 0, "jgmenu_run > ~/.cache/jgmenu/menu.csv"},
    {shift, XK_c, Action::Spawn, 0, "/usr/local/bin/capturecolor.sh"},
    {shift, XK_f, Action::Spawn, 0,
     "urxvtc +j -e \"$HOME/.local/bin/file-search\" || urxvt +j -e "
     "\"$HOME/.local/bin/file-search\""},
    {0, XK_Print, Action::Spawn, 0, "QT_QPA_PLATFORM=xcb flameshot gui"},
    {alt, XK_Print, Action::Spawn, 0, "flameshot full -p ~/Pictures/Screenshots"},
    {0, XF86XK_MonBrightnessUp, Action::Spawn, 0, "brightnessctl set 5%+"},
    {0, XF86XK_MonBrightnessDown, Action::Spawn, 0, "brightnessctl set 5%-"},
    {0, XF86XK_AudioLowerVolume, Action::Spawn, 0, "/usr/local/bin/skontrol down"},
    {0, XF86XK_AudioRaiseVolume, Action::Spawn, 0, "/usr/local/bin/skontrol up"},
    {0, XF86XK_AudioMute, Action::Spawn, 0, "/usr/local/bin/skontrol mute"},
    {alt, XK_j, Action::Focus, 1},
    {shift, XK_j, Action::FocusHidden, 1},
    {shift, XK_k, Action::FocusHidden, -1},
    {alt, XK_Left, Action::MasterCount, 1},
    {alt, XK_Right, Action::MasterCount, -1},
    {alt, XK_0, Action::AllTags},
    {shift, XK_0, Action::TagAll},
    {alt, XK_comma, Action::FocusMonitor, -1},
    {alt, XK_period, Action::FocusMonitor, 1},
    {shift, XK_comma, Action::SendMonitor, -1},
    {shift, XK_period, Action::SendMonitor, 1},
    {shift | ControlMask, XK_q, Action::Restart},
    {alt, XK_k, Action::Focus, -1},
    {alt, XK_l, Action::Ratio, -1},
    {alt, XK_i, Action::Ratio, 1},
    {alt, XK_t, Action::Layout, 0},
    {alt, XK_f, Action::Layout, 1},
    {alt, XK_m, Action::Layout, 2},
    {alt, XK_space, Action::CycleLayout},
    {shift, XK_space, Action::Float},
    {alt, XK_F11, Action::Fullscreen},
    {alt, XK_Return, Action::Zoom},
    {alt, XK_Tab, Action::PreviousTag},
    {alt, XK_b, Action::Bar},
    {alt, XK_h, Action::Hide},
    {shift, XK_s, Action::ShowAll},
    {alt, XK_grave, Action::Scratch},
    {shift, XK_Down, Action::Push, 1},
    {shift, XK_Up, Action::Push, -1},
    {alt, XK_q, Action::Close},
    {shift, XK_q, Action::Quit}};
} // namespace ppwm::config
