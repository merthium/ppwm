#include "window_manager.hpp"
#include "config.hpp"
#include <X11/Xatom.h>
#include <X11/cursorfont.h>
#include <X11/keysym.h>
#include <algorithm>
#include <cerrno>
#include <chrono>
#include <csignal>
#include <cstring>
#include <iostream>
#include <poll.h>
#include <stdexcept>
#include <unistd.h>
namespace ppwm {
namespace {
volatile std::sig_atomic_t running = 1;
bool ownership_error = false;
void stop(int) {
    running = 0;
}
int ownership_handler(Display *, XErrorEvent *event) {
    if (event->error_code == BadAccess)
        ownership_error = true;
    return 0;
}
int error_handler(Display *display, XErrorEvent *event) {
    if (event->error_code == BadWindow || event->error_code == BadDrawable ||
        event->error_code == BadMatch)
        return 0;
    char message[128];
    XGetErrorText(display, event->error_code, message, sizeof message);
    std::cerr << "ppwm: X11 " << message << " request " << static_cast<int>(event->request_code)
              << '\n';
    return 0;
}
} // namespace
Atom WindowManager::atom(const char *name) {
    auto [found, added] = atoms_.try_emplace(name, 0);
    if (added)
        found->second = XInternAtom(display_, name, False);
    return found->second;
}
Client *WindowManager::find_client(Window window) {
    auto found = std::find_if(clients_.begin(), clients_.end(),
                              [window](const Client &client) { return client.window == window; });
    return found == clients_.end() ? nullptr : &*found;
}
bool WindowManager::is_visible(const Client &client) const {
    return on_current_tags(client) && !client.hidden;
}
bool WindowManager::on_current_tags(const Client &client) const {
    return (client.tags & workspace_.active_tags()) != 0 && !client.swallowed_by;
}
void WindowManager::set_cardinal(Window window, const char *name, unsigned long v) {
    XChangeProperty(display_, window, atom(name), XA_CARDINAL, 32, PropModeReplace,
                    reinterpret_cast<unsigned char *>(&v), 1);
}
void WindowManager::set_window_property(Window window, const char *name, Window v) {
    XChangeProperty(display_, window, atom(name), XA_WINDOW, 32, PropModeReplace,
                    reinterpret_cast<unsigned char *>(&v), 1);
}
void WindowManager::set_client_state(Client &client, long value) {
    long data[]{value, None};
    XChangeProperty(display_, client.window, atom("WM_STATE"), atom("WM_STATE"), 32,
                    PropModeReplace, reinterpret_cast<unsigned char *>(data), 2);
}
WindowManager::WindowManager() : workspace_({.master_ratio = config::master_ratio}) {
    screen_number_ = DefaultScreen(display_);
    root_window_ = RootWindow(display_, screen_number_);
    screen_width_ = DisplayWidth(display_, screen_number_);
    screen_height_ = DisplayHeight(display_, screen_number_);
    XSetErrorHandler(ownership_handler);
    XSelectInput(display_, root_window_,
                 SubstructureRedirectMask | SubstructureNotifyMask | StructureNotifyMask);
    XSync(display_, False);
    if (ownership_error) {
        throw std::runtime_error("another window manager already owns this display");
    }
    XSetErrorHandler(error_handler);
    normal_cursor_ = XCreateFontCursor(display_, XC_left_ptr);
    move_cursor_ = XCreateFontCursor(display_, XC_fleur);
    resize_cursor_ = XCreateFontCursor(display_, XC_sizing);
    XDefineCursor(display_, root_window_, normal_cursor_);
    XColor color{}, exact{};
    XAllocNamedColor(display_, DefaultColormap(display_, screen_number_), config::normal_border,
                     &color, &exact);
    normal_border_ = color.pixel;
    XAllocNamedColor(display_, DefaultColormap(display_, screen_number_), config::selected_border,
                     &color, &exact);
    focused_border_ = color.pixel;
    support_window_ = XCreateSimpleWindow(display_, root_window_, -1, -1, 1, 1, 0, 0, 0);
    set_window_property(root_window_, "_NET_SUPPORTING_WM_CHECK", support_window_);
    set_window_property(support_window_, "_NET_SUPPORTING_WM_CHECK", support_window_);
    const char name[] = "ppwm";
    XChangeProperty(display_, support_window_, atom("_NET_WM_NAME"), atom("UTF8_STRING"), 8,
                    PropModeReplace, reinterpret_cast<const unsigned char *>(name), 4);
    std::vector<Atom> supported;
    for (const char *attributes :
         {"_NET_SUPPORTING_WM_CHECK", "_NET_WM_NAME", "_NET_ACTIVE_WINDOW", "_NET_CLIENT_LIST",
          "_NET_NUMBER_OF_DESKTOPS", "_NET_CURRENT_DESKTOP", "_NET_DESKTOP_NAMES",
          "_NET_WM_DESKTOP", "_NET_CLOSE_WINDOW", "_NET_WM_STATE", "_NET_WM_STATE_FULLSCREEN",
          "_NET_WM_WINDOW_TYPE", "_NET_WM_WINDOW_TYPE_DIALOG"})
        supported.push_back(atom(attributes));
    XChangeProperty(display_, root_window_, atom("_NET_SUPPORTED"), XA_ATOM, 32, PropModeReplace,
                    reinterpret_cast<unsigned char *>(supported.data()),
                    static_cast<int>(supported.size()));
    set_cardinal(root_window_, "_NET_NUMBER_OF_DESKTOPS", 9);
    set_cardinal(root_window_, "_NET_CURRENT_DESKTOP", 0);
    const char names[] = "1\0"
                         "2\0"
                         "3\0"
                         "4\0"
                         "5\0"
                         "6\0"
                         "7\0"
                         "8\0"
                         "9\0";
    XChangeProperty(display_, root_window_, atom("_NET_DESKTOP_NAMES"), atom("UTF8_STRING"), 8,
                    PropModeReplace, reinterpret_cast<const unsigned char *>(names),
                    sizeof(names) - 1);
    bar_ = std::make_unique<Bar>(display_, screen_number_, screen_width_);
    tray_ = std::make_unique<SystemTray>(display_, screen_number_, bar_->window(), screen_width_);
    grab_keys();
    std::signal(SIGTERM, stop);
    std::signal(SIGINT, stop);
    std::signal(SIGCHLD, SIG_IGN);
    Window rr, parent, *children = nullptr;
    unsigned count = 0;
    if (XQueryTree(display_, root_window_, &rr, &parent, &children, &count)) {
        for (unsigned i = 0; i < count; ++i) {
            XWindowAttributes attributes{};
            if (XGetWindowAttributes(display_, children[i], &attributes) &&
                attributes.map_state == IsViewable)
                manage_window(children[i]);
        }
        if (children)
            XFree(children);
    }
    update_client_list();
    arrange();
}
WindowManager::~WindowManager() {
    if (!display_)
        return;
    XUngrabPointer(display_, CurrentTime);
    XUngrabKey(display_, AnyKey, AnyModifier, root_window_);
    for (auto &client : clients_) {
        XUngrabButton(display_, AnyButton, AnyModifier, client.window);
        XSetWindowBorderWidth(display_, client.window,
                              static_cast<unsigned>(client.original_border));
        XMapWindow(display_, client.window);
        set_client_state(client, NormalState);
    }
    for (const char *p :
         {"_NET_SUPPORTED", "_NET_SUPPORTING_WM_CHECK", "_NET_ACTIVE_WINDOW", "_NET_CLIENT_LIST",
          "_NET_NUMBER_OF_DESKTOPS", "_NET_CURRENT_DESKTOP", "_NET_DESKTOP_NAMES"})
        XDeleteProperty(display_, root_window_, atom(p));
    XSetInputFocus(display_, PointerRoot, RevertToPointerRoot, CurrentTime);
    tray_.reset();
    bar_.reset();
    XDestroyWindow(display_, support_window_);
    XUndefineCursor(display_, root_window_);
    XFreeCursor(display_, normal_cursor_);
    XFreeCursor(display_, move_cursor_);
    XFreeCursor(display_, resize_cursor_);
    XSync(display_, False);
}
void WindowManager::quit() {
    running = 0;
}
void WindowManager::run() {
    using clock = std::chrono::steady_clock;
    auto deadline = clock::now();
    pollfd fd{ConnectionNumber(display_), POLLIN, 0};
    while (running) {
        // Bounded batches keep bar deadlines responsive during event floods.
        for (int i = 0; i < 256 && XPending(display_); ++i) {
            XEvent event;
            XNextEvent(display_, &event);
            if (event.type == MotionNotify) {
                // Coalesce only adjacent motion events. Never skip a button release.
                for (int pending = 0; pending < 256 && XEventsQueued(display_, QueuedAlready) > 0;
                     ++pending) {
                    XEvent next;
                    XPeekEvent(display_, &next);
                    if (next.type != MotionNotify || next.xmotion.window != event.xmotion.window)
                        break;
                    XNextEvent(display_, &event);
                }
            }
            handle_event(event);
        }
        auto now = clock::now();
        if (now >= deadline) {
            BarState bar_state;
            bar_state.active_tags = workspace_.active_tags();
            bar_state.layout = layouts[workspace_.settings().layout_index].symbol;
            for (const auto &client : clients_) {
                bar_state.occupied |= client.tags;
                if (on_current_tags(client))
                    bar_state.clients.push_back({client.window, client.title,
                                                 client.window == focused_window_, client.hidden});
            }
            if (auto *client = find_client(focused_window_))
                bar_state.floating = client->floating;
            bar_->render(bar_state, tray_->width());
            deadline = now + config::bar_interval;
        }
        if (XPending(display_))
            continue;
        XFlush(display_);
        int timeout = static_cast<int>(std::max<long long>(
            0, std::chrono::ceil<std::chrono::milliseconds>(deadline - clock::now()).count()));
        int result = poll(&fd, 1, timeout);
        if (result < 0 && errno != EINTR)
            throw std::runtime_error("X11 poll failed");
        if (result > 0 && (fd.revents & (POLLHUP | POLLERR | POLLNVAL)))
            break;
    }
}
} // namespace ppwm
