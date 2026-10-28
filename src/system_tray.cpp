#include "system_tray.hpp"
#include "config.hpp"
#include <X11/Xatom.h>
#include <algorithm>
#include <string>

namespace ppwm {
namespace {
constexpr long request_dock = 0;
constexpr long embedded_notify = 0;
constexpr long window_activate = 1;
constexpr long mapped_flag = 1;
} // namespace

SystemTray::SystemTray(Display *display, int screen, Window bar, int bar_width)
    : display_(display), root_(RootWindow(display, screen)), bar_(bar), bar_width_(bar_width) {
    const auto name = "_NET_SYSTEM_TRAY_S" + std::to_string(screen);
    selection_ = XInternAtom(display_, name.c_str(), False);
    opcode_ = XInternAtom(display_, "_NET_SYSTEM_TRAY_OPCODE", False);
    xembed_ = XInternAtom(display_, "_XEMBED", False);
    xembed_info_ = XInternAtom(display_, "_XEMBED_INFO", False);
    if (XGetSelectionOwner(display_, selection_) != None)
        return;

    window_ = XCreateSimpleWindow(display_, bar, 0, 0, 1, config::bar_height, 0, 0, 0);
    XStoreName(display_, window_, "ppwm-tray");
    XSelectInput(display_, window_, SubstructureRedirectMask | SubstructureNotifyMask);
    const auto orientation = XInternAtom(display_, "_NET_SYSTEM_TRAY_ORIENTATION", False);
    const auto visual_property = XInternAtom(display_, "_NET_SYSTEM_TRAY_VISUAL", False);
    unsigned long horizontal = 0;
    VisualID visual = XVisualIDFromVisual(DefaultVisual(display_, screen));
    XChangeProperty(display_, window_, orientation, XA_CARDINAL, 32, PropModeReplace,
                    reinterpret_cast<unsigned char *>(&horizontal), 1);
    XChangeProperty(display_, window_, visual_property, XA_VISUALID, 32, PropModeReplace,
                    reinterpret_cast<unsigned char *>(&visual), 1);
    XSetSelectionOwner(display_, selection_, window_, CurrentTime);
    if (XGetSelectionOwner(display_, selection_) != window_) {
        release();
        return;
    }

    XEvent event{};
    event.xclient.type = ClientMessage;
    event.xclient.window = root_;
    event.xclient.message_type = XInternAtom(display_, "MANAGER", False);
    event.xclient.format = 32;
    event.xclient.data.l[0] = CurrentTime;
    event.xclient.data.l[1] = static_cast<long>(selection_);
    event.xclient.data.l[2] = static_cast<long>(window_);
    XSendEvent(display_, root_, False, StructureNotifyMask, &event);
}

SystemTray::~SystemTray() {
    release();
}

SystemTray::Icon *SystemTray::find_icon(Window window) {
    auto found = std::find_if(icons_.begin(), icons_.end(),
                              [window](const Icon &icon) { return icon.window == window; });
    return found == icons_.end() ? nullptr : &*found;
}

Window SystemTray::requested_icon(const XEvent &event) const {
    if (window_ && event.type == ClientMessage && event.xclient.window == window_ &&
        event.xclient.message_type == opcode_ && event.xclient.format == 32 &&
        event.xclient.data.l[1] == request_dock)
        return static_cast<Window>(event.xclient.data.l[2]);
    return None;
}

void SystemTray::send_embed(Window icon, long message, long data1) {
    XEvent event{};
    event.xclient.type = ClientMessage;
    event.xclient.window = icon;
    event.xclient.message_type = xembed_;
    event.xclient.format = 32;
    event.xclient.data.l[0] = CurrentTime;
    event.xclient.data.l[1] = message;
    event.xclient.data.l[3] = data1;
    XSendEvent(display_, icon, False, NoEventMask, &event);
}

void SystemTray::dock(Window window) {
    if (window == None || window == window_ || window == root_ || window == bar_ ||
        find_icon(window))
        return;
    XWindowAttributes attributes{};
    if (!XGetWindowAttributes(display_, window, &attributes))
        return;
    // Save-set membership preserves external icons if this WM crashes.
    XAddToSaveSet(display_, window);
    XSelectInput(display_, window, StructureNotifyMask | PropertyChangeMask);
    XSetWindowBorderWidth(display_, window, 0);
    // ParentRelative backgrounds require matching depths; a solid background does not.
    XSetWindowBackground(display_, window, 0);
    XReparentWindow(display_, window, window_, 0, 0);
    icons_.push_back({window, false});
    update_mapped(icons_.back());
    send_embed(window, embedded_notify, static_cast<long>(window_));
    send_embed(window, window_activate);
}

void SystemTray::update_mapped(Icon &icon) {
    Atom type;
    int format;
    unsigned long count, remaining;
    unsigned char *bytes = nullptr;
    bool mapped = true; // Legacy tray clients may omit _XEMBED_INFO.
    if (XGetWindowProperty(display_, icon.window, xembed_info_, 0, 2, False, xembed_info_, &type,
                           &format, &count, &remaining, &bytes) == Success &&
        bytes) {
        if (format == 32 && count >= 2)
            mapped = (reinterpret_cast<unsigned long *>(bytes)[1] & mapped_flag) != 0;
        XFree(bytes);
    }
    icon.mapped = mapped;
    if (mapped)
        XMapRaised(display_, icon.window);
    else
        XUnmapWindow(display_, icon.window);
    layout();
}

void SystemTray::layout() {
    if (!window_)
        return;
    int x = config::tray_spacing;
    for (const auto &icon : icons_) {
        if (!icon.mapped)
            continue;
        XMoveResizeWindow(display_, icon.window, x,
                          (config::bar_height - config::tray_icon_size) / 2, config::tray_icon_size,
                          config::tray_icon_size);
        x += config::tray_icon_size + config::tray_spacing;
    }
    width_ = x == config::tray_spacing ? 0 : std::min(x, bar_width_);
    XMoveResizeWindow(display_, window_, std::max(0, bar_width_ - width_), 0,
                      static_cast<unsigned>(std::max(1, width_)), config::bar_height);
    if (width_)
        XMapRaised(display_, window_);
    else
        XUnmapWindow(display_, window_);
}

void SystemTray::resize(int bar_width) {
    bar_width_ = bar_width;
    layout();
}

void SystemTray::remove(Window window, bool destroyed) {
    if (!find_icon(window))
        return;
    if (!destroyed)
        XRemoveFromSaveSet(display_, window);
    std::erase_if(icons_, [window](const Icon &icon) { return icon.window == window; });
    layout();
}

void SystemTray::release() {
    if (!window_)
        return;
    for (const auto &icon : icons_) {
        XRemoveFromSaveSet(display_, icon.window);
        XUnmapWindow(display_, icon.window);
        XReparentWindow(display_, icon.window, root_, 0, 0);
    }
    icons_.clear();
    if (XGetSelectionOwner(display_, selection_) == window_)
        XSetSelectionOwner(display_, selection_, None, CurrentTime);
    XDestroyWindow(display_, window_);
    window_ = None;
    width_ = 0;
}

bool SystemTray::handle_event(const XEvent &event) {
    if (auto icon = requested_icon(event); icon != None) {
        dock(icon);
        return true;
    }
    if (event.type == SelectionClear && event.xselectionclear.selection == selection_) {
        release();
        return true;
    }
    Window target = event.xany.window;
    if (event.type == DestroyNotify)
        target = event.xdestroywindow.window;
    if (event.type == ReparentNotify)
        target = event.xreparent.window;
    if (event.type == UnmapNotify)
        target = event.xunmap.window;
    if (event.type == ConfigureRequest)
        target = event.xconfigurerequest.window;
    if (event.type == MapRequest)
        target = event.xmaprequest.window;
    auto *icon = find_icon(target);
    if (!icon)
        return false;
    switch (event.type) {
    case DestroyNotify:
        remove(target, true);
        break;
    case ReparentNotify:
        if (event.xreparent.parent != window_)
            remove(target, false);
        break;
    case PropertyNotify:
        if (event.xproperty.atom == xembed_info_)
            update_mapped(*icon);
        break;
    case MapRequest:
    case UnmapNotify:
        update_mapped(*icon);
        break;
    case ConfigureRequest:
        layout();
        break;
    default:
        break;
    }
    return true;
}
} // namespace ppwm
