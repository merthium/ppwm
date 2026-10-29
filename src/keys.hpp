#pragma once
#include <X11/Xlib.h>
namespace ppwm::config {
enum class Action {
    Spawn,
    ViewTag,
    ToggleViewTag,
    SendTag,
    ToggleSendTag,
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
enum class MouseContext { Client, Tag, Layout, Title, StatusArea };
enum class MouseAction {
    Move,
    Resize,
    Float,
    ViewTag,
    ToggleViewTag,
    SendTag,
    ToggleSendTag,
    CycleLayout,
    Layout,
    ToggleHide,
    FocusZoom,
    ModuleCommand,
    Spawn
};
struct MouseBinding {
    MouseContext context;
    unsigned modifiers;
    unsigned button;
    MouseAction action;
    int argument{};
    const char *command{};
};
} // namespace ppwm::config
