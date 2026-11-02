#include "ppwm.hpp"
#include "config.hpp"
#include <X11/Xatom.h>
#include <X11/cursorfont.h>
#include <X11/extensions/XRes.h>
#include <X11/keysym.h>
#include <algorithm>
#include <array>
#include <cctype>
#include <cerrno>
#include <charconv>
#include <chrono>
#include <csignal>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <fcntl.h>
#include <fstream>
#include <iostream>
#include <limits>
#include <memory>
#include <poll.h>
#include <stdexcept>
#include <string>
#include <sys/file.h>
#include <unistd.h>
#include <unordered_map>

// ---- Window Manager ----
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
WindowManager::WindowManager()
    : workspace_({.master_ratio = config::master_ratio,
                  .master_count = config::default_master_count,
                  .layout_index = config::default_layout % config::layouts.size(),
                  .previous_layout = config::default_previous_layout % config::layouts.size(),
                  .bar_visible = config::default_bar_visible}) {
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
    normal_cursor_ = XCreateFontCursor(display_, config::normal_cursor);
    move_cursor_ = XCreateFontCursor(display_, config::move_cursor);
    resize_cursor_ = XCreateFontCursor(display_, config::resize_cursor);
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
    std::string names;
    for (const auto label : config::tag_labels) {
        names.append(label);
        names.push_back('\0');
    }
    XChangeProperty(display_, root_window_, atom("_NET_DESKTOP_NAMES"), atom("UTF8_STRING"), 8,
                    PropModeReplace, reinterpret_cast<const unsigned char *>(names.data()),
                    static_cast<int>(names.size()));
    bar_ = std::make_unique<Bar>(display_, screen_number_, screen_width_);
    tray_ = std::make_unique<SystemTray>(display_, screen_number_, bar_->window(), screen_width_);
    grab_keys();
    std::signal(SIGTERM, stop);
    std::signal(SIGINT, stop);
    std::signal(SIGCHLD, SIG_IGN);
    load_session();
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
    restore_session();
    update_client_list();
    arrange();
}
WindowManager::~WindowManager() {
    if (!display_)
        return;
    if (restart_requested_)
        save_session();
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
        for (int i = 0; i < config::event_batch_limit && XPending(display_); ++i) {
            XEvent event;
            XNextEvent(display_, &event);
            if (event.type == MotionNotify) {
                // Coalesce only adjacent motion events. Never skip a button release.
                for (int pending = 0; pending < config::event_batch_limit &&
                                      XEventsQueued(display_, QueuedAlready) > 0;
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
            tray_->refresh();
            BarState bar_state;
            bar_state.clients.reserve(clients_.size());
            bar_state.active_tags = workspace_.active_tags();
            bar_state.layout = config::layouts[workspace_.settings().layout_index].symbol;
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

// ---- Clients ----
namespace ppwm {

void WindowManager::update_client_list() {
    std::vector<Window> windows;
    for (const auto &client : clients_)
        windows.push_back(client.window);
    XChangeProperty(display_, root_window_, atom("_NET_CLIENT_LIST"), XA_WINDOW, 32,
                    PropModeReplace, reinterpret_cast<unsigned char *>(windows.data()),
                    static_cast<int>(windows.size()));
}
void WindowManager::manage_window(Window window) {
    if (find_client(window) || window == support_window_ || window == bar_->window())
        return;
    XWindowAttributes attributes{};
    if (!XGetWindowAttributes(display_, window, &attributes) || attributes.override_redirect)
        return;
    Client client{};
    client.window = window;
    client.title = window_title(window);
    client.tags = workspace_.active_tags();
    client.mapped = attributes.map_state == IsViewable;
    client.original_border = attributes.border_width;
    client.geometry = {attributes.x, attributes.y, attributes.width, attributes.height};
    client.floating_geometry = client.geometry;
    Window transient{};
    if (XGetTransientForHint(display_, window, &transient)) {
        client.floating = true;
        if (auto *parent = find_client(transient))
            client.tags = parent->tags;
    }
    XClassHint cls{};
    if (XGetClassHint(display_, window, &cls)) {
        if (cls.res_class) {
            std::string name(cls.res_class);
            std::transform(name.begin(), name.end(), name.begin(), [](unsigned char character) {
                return static_cast<char>(std::tolower(character));
            });
            client.terminal =
                std::find(config::terminal_classes.begin(), config::terminal_classes.end(), name) !=
                config::terminal_classes.end();
        }
        for (auto rule : config::rules)
            if ((!rule.class_name ||
                 (cls.res_class && std::strcmp(cls.res_class, rule.class_name) == 0)) &&
                (!rule.instance ||
                 (cls.res_name && std::strcmp(cls.res_name, rule.instance) == 0))) {
                client.floating = rule.floating;
                client.scratchpad = rule.scratchpad;
                if (rule.tag >= 1 && rule.tag <= 9)
                    client.tags = 1u << (rule.tag - 1);
            }
        if (cls.res_name)
            XFree(cls.res_name);
        if (cls.res_class)
            XFree(cls.res_class);
    }
    XSizeHints size{};
    long supplied{};
    if (XGetWMNormalHints(display_, window, &size, &supplied) && (size.flags & PMinSize) &&
        (size.flags & PMaxSize) && size.min_width == size.max_width &&
        size.min_height == size.max_height)
        client.floating = true;
    if (auto *hints = XGetWMHints(display_, window)) {
        if (hints->flags & InputHint)
            client.input = hints->input;
        XFree(hints);
    }
    Atom type;
    int fmt;
    unsigned long n, left;
    unsigned char *data = nullptr;
    if (XGetWindowProperty(display_, window, atom("_NET_WM_WINDOW_TYPE"), 0, 32, False, XA_ATOM,
                           &type, &fmt, &n, &left, &data) == Success &&
        data) {
        if (fmt == 32)
            for (unsigned long i = 0; i < n; ++i)
                if (reinterpret_cast<Atom *>(data)[i] == atom("_NET_WM_WINDOW_TYPE_DIALOG"))
                    client.floating = true;
        XFree(data);
    }
    bool full = false;
    data = nullptr;
    if (XGetWindowProperty(display_, window, atom("_NET_WM_STATE"), 0, 32, False, XA_ATOM, &type,
                           &fmt, &n, &left, &data) == Success &&
        data) {
        if (fmt == 32)
            for (unsigned long i = 0; i < n; ++i)
                if (reinterpret_cast<Atom *>(data)[i] == atom("_NET_WM_STATE_FULLSCREEN"))
                    full = true;
        XFree(data);
    }
    XSelectInput(display_, window, PropertyChangeMask | EnterWindowMask);
    client.border_width = config::border;
    XSetWindowBorderWidth(display_, window, static_cast<unsigned>(client.border_width));
    XSetWindowBorder(display_, window, normal_border_);
    grab_buttons(client, false);
    clients_.insert(clients_.begin(), client);
    auto &added = clients_.front();
    if (restoring_) {
        added.fullscreen = full;
        return;
    }
    publish_client_tags(added);
    set_client_state(added, NormalState);
    if (full)
        set_fullscreen(added, true);
    try_swallow(window, transient != None);
    arrange();
    if (auto *managed = find_client(window); managed && is_visible(*managed))
        focus(window);
    update_client_list();
}
void WindowManager::try_swallow(Window window, bool transient) {
    auto *child = find_client(window);
    if (!config::swallow_automatic || !child || child->terminal || transient)
        return;
    const auto pid = processes_.window_pid(window);
    const auto terminal_window = pid ? processes_.terminal_window(*pid) : std::nullopt;
    if (!terminal_window)
        return;
    auto *terminal = find_client(*terminal_window);
    if (!terminal || !terminal->terminal || terminal->swallowed_by || terminal->terminal_window ||
        terminal->hidden)
        return;
    terminal->swallowed_by = window;
    child->terminal_window = terminal->window;
    child->tags = terminal->tags;
    child->floating = terminal->floating;
    child->floating_geometry = terminal->floating_geometry;
    if (terminal->fullscreen)
        set_fullscreen(*child, true);
    set_geometry(*child, terminal->geometry);
    publish_client_tags(*child);
    // New clients start at the front. Insert this child directly after its hidden terminal.
    const auto terminal_index = static_cast<std::size_t>(terminal - clients_.data());
    std::rotate(clients_.begin(), clients_.begin() + 1,
                clients_.begin() + static_cast<std::ptrdiff_t>(terminal_index + 1));
}
void WindowManager::unmanage_window(Window window, bool destroyed) {
    auto *client = find_client(window);
    if (!client)
        return;
    const Client removed = *client;
    if (drag_.window == window) {
        drag_ = {};
        XUngrabPointer(display_, CurrentTime);
    }
    if (preview_window_ == window)
        preview_window_ = None;
    if (!destroyed) {
        set_client_state(*client, WithdrawnState);
        XUngrabButton(display_, AnyButton, AnyModifier, window);
        XSetWindowBorderWidth(display_, window, static_cast<unsigned>(client->original_border));
    }
    if (auto *child = find_client(removed.swallowed_by))
        child->terminal_window = None;
    std::erase_if(clients_, [window](const Client &item) { return item.window == window; });
    if (focused_window_ == window)
        focused_window_ = None;
    if (auto *terminal = find_client(removed.terminal_window)) {
        terminal->swallowed_by = None;
        terminal->tags = removed.tags;
        terminal->floating = removed.floating;
        terminal->floating_geometry = removed.floating_geometry;
        set_fullscreen(*terminal, removed.fullscreen);
        publish_client_tags(*terminal);
    }
    arrange();
    update_client_list();
}
void WindowManager::send_configure_notify(Client &client) {
    XEvent event{};
    event.xconfigure.type = ConfigureNotify;
    event.xconfigure.display = display_;
    event.xconfigure.event = client.window;
    event.xconfigure.window = client.window;
    event.xconfigure.x = client.geometry.x;
    event.xconfigure.y = client.geometry.y;
    event.xconfigure.width = client.geometry.width;
    event.xconfigure.height = client.geometry.height;
    event.xconfigure.border_width = client.border_width;
    XSendEvent(display_, client.window, False, StructureNotifyMask, &event);
}
void WindowManager::set_geometry(Client &client, Rect geometry) {
    geometry.width = std::max(1, geometry.width);
    geometry.height = std::max(1, geometry.height);
    if (client.geometry == geometry)
        return;
    client.geometry = geometry;
    XMoveResizeWindow(display_, client.window, client.geometry.x, client.geometry.y,
                      static_cast<unsigned>(client.geometry.width),
                      static_cast<unsigned>(client.geometry.height));
    send_configure_notify(client);
}
void WindowManager::arrange() {
    const auto area = workarea();
    XMoveWindow(display_, bar_->window(), 0,
                config::topbar ? 0 : std::max(0, screen_height_ - config::bar_height));
    const bool single = config::smart_borders && std::count_if(clients_.begin(), clients_.end(),
                                                               [this](const Client &client) {
                                                                   return is_visible(client);
                                                               }) == 1;
    std::vector<Client *> tiled;
    tiled.reserve(clients_.size());
    for (auto &client : clients_) {
        if (!is_visible(client)) {
            if (client.mapped) {
                ++client.ignore_unmap;
                XUnmapWindow(display_, client.window);
                client.mapped = false;
            }
            continue;
        }
        const int border = client.fullscreen || single ? 0 : config::border;
        if (client.border_width != border) {
            client.border_width = border;
            XSetWindowBorderWidth(display_, client.window, static_cast<unsigned>(border));
            send_configure_notify(client);
        }
        if (!client.mapped) {
            XMapWindow(display_, client.window);
            client.mapped = true;
        }
        if (client.fullscreen) {
            set_geometry(client, {0, 0, screen_width_, screen_height_});
            continue;
        }
        if (client.floating || !config::layouts[workspace_.settings().layout_index].arrange) {
            set_geometry(client,
                         constrain_floating(client.floating_geometry, area, client.border_width));
        } else
            tiled.push_back(&client);
    }
    if (config::layouts[workspace_.settings().layout_index].arrange) {
        auto rects = config::layouts[workspace_.settings().layout_index].arrange(
            area, static_cast<int>(tiled.size()), workspace_.settings().master_ratio, config::gap,
            workspace_.settings().master_count);
        for (std::size_t i = 0; i < tiled.size(); ++i) {
            auto geometry = rects[i];
            geometry.width -= 2 * tiled[i]->border_width;
            geometry.height -= 2 * tiled[i]->border_width;
            set_geometry(*tiled[i], geometry);
        }
    }
    auto *selected = find_client(focused_window_);
    if (!selected || !is_visible(*selected)) {
        const Client *recent = nullptr;
        for (const auto &client : clients_)
            if (is_visible(client) && (!recent || client.focus_order > recent->focus_order))
                recent = &client;
        focus(recent ? recent->window : None);
    }
    for (const auto &client : clients_) {
        if (is_visible(client) && client.fullscreen) {
            focus(client.window);
            break;
        }
    }
    restack();
    XFlush(display_);
}
void WindowManager::restack() {
    auto *selected = find_client(focused_window_);
    if (selected && is_visible(*selected) &&
        config::layouts[workspace_.settings().layout_index].arrange == monocle)
        XRaiseWindow(display_, selected->window);
    for (auto &client : clients_)
        if (is_visible(client) &&
            (client.floating || !config::layouts[workspace_.settings().layout_index].arrange))
            XRaiseWindow(display_, client.window);
    if (selected && is_visible(*selected) &&
        (selected->floating || !config::layouts[workspace_.settings().layout_index].arrange))
        XRaiseWindow(display_, selected->window);
    if (workspace_.settings().bar_visible)
        XRaiseWindow(display_, bar_->window());
    for (auto &client : clients_)
        if (is_visible(client) && client.fullscreen)
            XRaiseWindow(display_, client.window);
    // A combined tag view can contain fullscreen clients from disjoint tags.
    // Keep their states, but never cover the client that receives keyboard input.
    if (selected && is_visible(*selected) && selected->fullscreen)
        XRaiseWindow(display_, selected->window);
    // Our own stacking changes generate crossings; do not turn those into a focus loop.
    XSync(display_, False);
    XEvent crossing;
    while (XCheckMaskEvent(display_, EnterWindowMask, &crossing)) {
    }
}
void WindowManager::focus(Window window) {
    // Fullscreen locks focus on its tag until it is exited.
    for (auto &client : clients_)
        if (is_visible(client) && client.fullscreen) {
            window = client.window;
            break;
        }
    if (window == focused_window_ && find_client(window))
        return;
    auto *next = find_client(window);
    if (next && !is_visible(*next))
        return;
    bool preview_closed = false;
    if (preview_window_ && preview_window_ != window) {
        if (auto *preview = find_client(preview_window_)) {
            preview->hidden = true;
            set_client_state(*preview, IconicState);
            if (preview->mapped) {
                ++preview->ignore_unmap;
                XUnmapWindow(display_, preview->window);
                preview->mapped = false;
            }
            preview_closed = true;
        }
        preview_window_ = None;
    }
    if (auto *old = find_client(focused_window_)) {
        XSetWindowBorder(display_, old->window, normal_border_);
        grab_buttons(*old, false);
    }
    focused_window_ = next ? window : None;
    if (!next) {
        XSetInputFocus(display_, root_window_, RevertToPointerRoot, CurrentTime);
        XDeleteProperty(display_, root_window_, atom("_NET_ACTIVE_WINDOW"));
        return;
    }
    next->focus_order = ++focus_serial_;
    grab_buttons(*next, true);
    XSetWindowBorder(display_, window, focused_border_);
    if (next->input)
        XSetInputFocus(display_, window, RevertToPointerRoot, CurrentTime);
    Atom *protocols = nullptr;
    int count = 0;
    if (XGetWMProtocols(display_, window, &protocols, &count)) {
        for (int i = 0; i < count; ++i)
            if (protocols[i] == atom("WM_TAKE_FOCUS")) {
                XEvent event{};
                event.xclient.type = ClientMessage;
                event.xclient.window = window;
                event.xclient.message_type = atom("WM_PROTOCOLS");
                event.xclient.format = 32;
                event.xclient.data.l[0] = static_cast<long>(atom("WM_TAKE_FOCUS"));
                event.xclient.data.l[1] = CurrentTime;
                XSendEvent(display_, window, False, NoEventMask, &event);
            }
        XFree(protocols);
    }
    restack();
    set_window_property(root_window_, "_NET_ACTIVE_WINDOW", window);
    if (preview_closed)
        arrange();
}
void WindowManager::cycle_focus(int step, bool include_hidden) {
    if (auto *selected = find_client(focused_window_); selected && selected->fullscreen)
        return;
    std::vector<Window> windows;
    for (const auto &client : clients_)
        if (on_current_tags(client) && (include_hidden || !client.hidden))
            windows.push_back(client.window);
    if (windows.empty())
        return;
    auto found = std::find(windows.begin(), windows.end(), focused_window_);
    int index =
        found == windows.end() ? (step > 0 ? -1 : 0) : static_cast<int>(found - windows.begin());
    int count = static_cast<int>(windows.size());
    auto *target = find_client(windows[static_cast<std::size_t>((index + step + count) % count)]);
    const bool hidden = target->hidden;
    if (hidden) {
        target->hidden = false;
        target->mapped = true;
        XMapWindow(display_, target->window);
        set_client_state(*target, NormalState);
    }
    focus(target->window);
    preview_window_ = hidden ? target->window : None;
    arrange();
    if (config::warp_pointer)
        XWarpPointer(display_, None, target->window, 0, 0, 0, 0, target->geometry.width / 2,
                     target->geometry.height / 2);
}
void WindowManager::view_tag(unsigned tag) {
    if (tag < 9)
        view_tags(1u << tag);
}
void WindowManager::view_tags(TagMask tags, bool toggle) {
    bool changed = toggle ? workspace_.toggle(tags) : workspace_.view(tags);
    if (!changed)
        return;
    set_cardinal(root_window_, "_NET_CURRENT_DESKTOP", workspace_.primary_tag());
    if (workspace_.settings().bar_visible)
        XMapRaised(display_, bar_->window());
    else
        XUnmapWindow(display_, bar_->window());
    arrange();
}
void WindowManager::publish_client_tags(const Client &client) {
    set_cardinal(client.window, "_NET_WM_DESKTOP",
                 client.tags == all_tags ? 0xffffffffUL : first_tag(client.tags));
}
void WindowManager::assign_tag(Client &client, unsigned tag, bool follow) {
    if (tag < 9)
        assign_tags(client, 1u << tag, follow);
}
void WindowManager::assign_tags(Client &client, TagMask tags, bool follow, bool toggle) {
    tags &= all_tags;
    if (toggle)
        tags ^= client.tags;
    if (tags == 0)
        return;
    client.tags = tags;
    if (client.fullscreen)
        for (auto &other : clients_)
            if (other.window != client.window && (other.tags & tags) && other.fullscreen)
                set_fullscreen(other, false);
    publish_client_tags(client);
    if (follow)
        view_tags(tags);
    arrange();
    if (client.fullscreen && is_visible(client))
        focus(client.window);
}
void WindowManager::select_layout(std::size_t index) {
    workspace_.select_layout(index % config::layouts.size());
    arrange();
}
void WindowManager::toggle_layout() {
    workspace_.toggle_layout();
    arrange();
}
Rect WindowManager::workarea() const {
    int height = workspace_.settings().bar_visible ? config::bar_height : 0;
    return {0, config::topbar ? height : 0, screen_width_, std::max(1, screen_height_ - height)};
}
void WindowManager::set_fullscreen(Client &client, bool enabled) {
    if (client.fullscreen == enabled)
        return;
    if (enabled) {
        for (auto &other : clients_)
            if (other.window != client.window && (other.tags & client.tags) && other.fullscreen)
                set_fullscreen(other, false);
    }
    client.fullscreen = enabled;
    const Atom full = atom("_NET_WM_STATE_FULLSCREEN");
    const Atom property = atom("_NET_WM_STATE");
    Atom type{};
    int format{};
    unsigned long count{}, remaining{};
    unsigned char *raw = nullptr;
    std::vector<Atom> states;
    if (XGetWindowProperty(display_, client.window, property, 0, 1024, False, XA_ATOM, &type,
                           &format, &count, &remaining, &raw) == Success &&
        raw && type == XA_ATOM && format == 32) {
        const auto *values = reinterpret_cast<const Atom *>(raw);
        states.assign(values, values + count);
    }
    if (raw)
        XFree(raw);
    std::erase(states, full);
    if (enabled)
        states.push_back(full);
    XChangeProperty(display_, client.window, property, XA_ATOM, 32, PropModeReplace,
                    reinterpret_cast<const unsigned char *>(states.data()),
                    static_cast<int>(states.size()));
    arrange();
    if (enabled && is_visible(client))
        focus(client.window);
}
void WindowManager::toggle_floating(Client &client) {
    if (client.fullscreen)
        return;
    client.floating = !client.floating;
    arrange();
    focus(client.window);
}
void WindowManager::close_window(Client &client) {
    Atom *protocols = nullptr;
    int count = 0;
    bool supports = false;
    if (XGetWMProtocols(display_, client.window, &protocols, &count)) {
        for (int i = 0; i < count; ++i)
            if (protocols[i] == atom("WM_DELETE_WINDOW"))
                supports = true;
        XFree(protocols);
    }
    if (supports) {
        XEvent event{};
        event.xclient.type = ClientMessage;
        event.xclient.window = client.window;
        event.xclient.message_type = atom("WM_PROTOCOLS");
        event.xclient.format = 32;
        event.xclient.data.l[0] = static_cast<long>(atom("WM_DELETE_WINDOW"));
        event.xclient.data.l[1] = CurrentTime;
        XSendEvent(display_, client.window, False, NoEventMask, &event);
    } else
        XKillClient(display_, client.window);
}
std::string WindowManager::window_title(Window window) {
    Atom type;
    int format;
    unsigned long n, left;
    unsigned char *data = nullptr;
    std::string result;
    if (XGetWindowProperty(display_, window, atom("_NET_WM_NAME"), 0, 256, False,
                           atom("UTF8_STRING"), &type, &format, &n, &left, &data) == Success &&
        data) {
        if (format == 8)
            result.assign(reinterpret_cast<char *>(data), n);
        XFree(data);
    }
    if (result.empty()) {
        char *legacy = nullptr;
        if (XFetchName(display_, window, &legacy) && legacy) {
            result = legacy;
            XFree(legacy);
        }
    }
    return result;
}
} // namespace ppwm

// ---- Events ----
namespace ppwm {
void WindowManager::handle_configure_request(const XConfigureRequestEvent &request) {
    auto *client = find_client(request.window);
    if (!client) {
        XWindowChanges changes{
            request.x,     request.y,     request.width, request.height, request.border_width,
            request.above, request.detail};
        XConfigureWindow(display_, request.window, static_cast<unsigned>(request.value_mask),
                         &changes);
        return;
    }

    const bool floating_layout =
        config::layouts[workspace_.settings().layout_index].arrange == nullptr;
    if (client->fullscreen || (!client->floating && !floating_layout)) {
        // Tiled clients cannot choose their geometry, but still need an ICCCM reply.
        send_configure_notify(*client);
        return;
    }

    auto &geometry = client->floating_geometry;
    if (request.value_mask & CWX)
        geometry.x = request.x;
    if (request.value_mask & CWY)
        geometry.y = request.y;
    if (request.value_mask & CWWidth)
        geometry.width = request.width;
    if (request.value_mask & CWHeight)
        geometry.height = request.height;
    const auto previous = client->geometry;
    set_geometry(*client, constrain_floating(geometry, workarea(), client->border_width));
    if (previous == client->geometry)
        send_configure_notify(*client);
}

void WindowManager::handle_motion(const XMotionEvent &motion) {
    auto *client = find_client(drag_.window);
    if (!client)
        return;

    auto geometry = drag_.original;
    const int dx = motion.x_root - drag_.x;
    const int dy = motion.y_root - drag_.y;
    if (drag_.resize) {
        geometry.width = std::max(config::minimum_float_size, geometry.width + dx);
        geometry.height = std::max(config::minimum_float_size, geometry.height + dy);
    } else {
        geometry.x += dx;
        geometry.y += dy;
        const auto area = workarea();
        const int right = area.x + area.width - geometry.width - 2 * client->border_width;
        const int bottom = area.y + area.height - geometry.height - 2 * client->border_width;
        if (std::abs(geometry.x - area.x) < config::snap)
            geometry.x = area.x;
        if (std::abs(geometry.y - area.y) < config::snap)
            geometry.y = area.y;
        if (std::abs(geometry.x - right) < config::snap)
            geometry.x = right;
        if (std::abs(geometry.y - bottom) < config::snap)
            geometry.y = bottom;
    }
    client->floating_geometry = geometry;
    set_geometry(*client, geometry);
}

void WindowManager::handle_client_message(const XClientMessageEvent &message) {
    if (message.format != 32)
        return;
    const long argument = message.data.l[0];
    const bool valid_tag = argument >= 0 && argument < 9;
    if (message.message_type == atom("_NET_CURRENT_DESKTOP")) {
        if (valid_tag)
            view_tag(static_cast<unsigned>(argument));
        return;
    }
    auto *client = find_client(message.window);
    if (!client)
        return;

    if (message.message_type == atom("_NET_WM_DESKTOP")) {
        if (static_cast<unsigned long>(argument) == 0xffffffffUL || argument == -1)
            assign_tags(*client, all_tags);
        else if (valid_tag)
            assign_tag(*client, static_cast<unsigned>(argument));
    } else if (message.message_type == atom("_NET_ACTIVE_WINDOW")) {
        if (preview_window_ == client->window)
            preview_window_ = None;
        client->hidden = false;
        set_client_state(*client, NormalState);
        if (!(client->tags & workspace_.active_tags()))
            view_tags(client->tags);
        // view_tags is a no-op when already there; activation must still map a hidden client.
        arrange();
        focus(client->window);
    } else if (message.message_type == atom("_NET_CLOSE_WINDOW")) {
        close_window(*client);
    } else if (message.message_type == atom("_NET_WM_STATE")) {
        const Atom fullscreen = atom("_NET_WM_STATE_FULLSCREEN");
        const bool changes_fullscreen = static_cast<Atom>(message.data.l[1]) == fullscreen ||
                                        static_cast<Atom>(message.data.l[2]) == fullscreen;
        if (changes_fullscreen && argument >= 0 && argument <= 2) {
            // EWMH actions: 0 removes, 1 adds, 2 toggles a state.
            set_fullscreen(*client, argument == 2 ? !client->fullscreen : argument == 1);
        }
    }
}

void WindowManager::handle_event(XEvent &event) {
    if (auto icon = tray_->requested_icon(event); icon != None && find_client(icon))
        unmanage_window(icon, false);
    if (tray_->handle_event(event))
        return;
    switch (event.type) {
    case MapRequest:
        if (auto *client = find_client(event.xmaprequest.window)) {
            if (preview_window_ == client->window)
                preview_window_ = None;
            client->hidden = false;
            set_client_state(*client, NormalState);
            arrange();
        } else {
            manage_window(event.xmaprequest.window);
        }
        break;
    case DestroyNotify:
        unmanage_window(event.xdestroywindow.window, true);
        break;
    case UnmapNotify:
        if (auto *client = find_client(event.xunmap.window)) {
            // Hiding a tag generates UnmapNotify too; it is not a client withdrawal.
            if (client->ignore_unmap > 0)
                --client->ignore_unmap;
            else
                unmanage_window(client->window, false);
        }
        break;
    case ConfigureRequest:
        handle_configure_request(event.xconfigurerequest);
        break;
    case ConfigureNotify:
        if (event.xconfigure.window == root_window_ &&
            (screen_width_ != event.xconfigure.width ||
             screen_height_ != event.xconfigure.height)) {
            screen_width_ = event.xconfigure.width;
            screen_height_ = event.xconfigure.height;
            bar_->resize(screen_width_);
            tray_->resize(screen_width_);
            arrange();
        }
        break;
    case EnterNotify:
        if (config::focus_follows_mouse && event.xcrossing.mode == NotifyNormal &&
            event.xcrossing.detail != NotifyInferior && !drag_.window &&
            find_client(event.xcrossing.window))
            focus(event.xcrossing.window);
        break;
    case KeyPress:
        handle_key_press(event.xkey);
        break;
    case ButtonPress:
        handle_button_press(event.xbutton);
        break;
    case MotionNotify:
        handle_motion(event.xmotion);
        break;
    case ButtonRelease:
        if (drag_.window) {
            drag_ = {};
            XUngrabPointer(display_, event.xbutton.time);
        }
        break;
    case MappingNotify:
        XRefreshKeyboardMapping(&event.xmapping);
        grab_keys();
        break;
    case PropertyNotify:
        if (auto *client = find_client(event.xproperty.window)) {
            if (event.xproperty.atom == XA_WM_NAME || event.xproperty.atom == atom("_NET_WM_NAME"))
                client->title = window_title(client->window);
            if (event.xproperty.atom == XA_WM_HINTS) {
                if (auto *hints = XGetWMHints(display_, client->window)) {
                    client->input = !(hints->flags & InputHint) || hints->input;
                    XFree(hints);
                }
            }
        }
        break;
    case ClientMessage:
        handle_client_message(event.xclient);
        break;
    default:
        break;
    }
}
} // namespace ppwm

// ---- Input ----
namespace ppwm {
void WindowManager::spawn(const char *command) {
    if (!command)
        return;
    if (fork() == 0) {
        ::close(ConnectionNumber(display_));
        setsid();
        execl("/bin/sh", "sh", "-c", command, static_cast<char *>(nullptr));
        _exit(127);
    }
}
void WindowManager::grab_buttons(const Client &client, bool focused) {
    XUngrabButton(display_, AnyButton, AnyModifier, client.window);
    // Pause an unfocused client's click until focus is updated, then replay it to the app.
    if (!focused)
        XGrabButton(display_, AnyButton, AnyModifier, client.window, False, ButtonPressMask,
                    GrabModeSync, GrabModeAsync, None, None);
    for (unsigned lock :
         {0u, static_cast<unsigned>(LockMask), numlock_mask_, numlock_mask_ | LockMask})
        for (const auto &binding : config::mouse_bindings)
            if (binding.context == config::MouseContext::Client)
                XGrabButton(display_, binding.button, binding.modifiers | lock, client.window,
                            False, ButtonPressMask | ButtonReleaseMask | PointerMotionMask,
                            GrabModeAsync, GrabModeAsync, None, None);
}
void WindowManager::grab_keys() {
    numlock_mask_ = 0;
    if (auto *map = XGetModifierMapping(display_)) {
        auto code = XKeysymToKeycode(display_, XK_Num_Lock);
        for (int m = 0; m < 8; ++m)
            for (int i = 0; i < map->max_keypermod; ++i)
                if (map->modifiermap[m * map->max_keypermod + i] == code)
                    numlock_mask_ = 1u << m;
        XFreeModifiermap(map);
    }
    XUngrabKey(display_, AnyKey, AnyModifier, root_window_);
    auto grab = [&](KeySym keysym, unsigned modifiers) {
        auto code = XKeysymToKeycode(display_, keysym);
        if (!code)
            return;
        for (unsigned lock :
             {0u, static_cast<unsigned>(LockMask), numlock_mask_, numlock_mask_ | LockMask})
            XGrabKey(display_, code, modifiers | lock, root_window_, True, GrabModeAsync,
                     GrabModeAsync);
    };
    for (const auto &binding : config::keys)
        grab(binding.key, binding.modifiers);
}
void WindowManager::promote_to_master() {
    auto *selected = find_client(focused_window_);
    if (!selected || selected->floating || selected->fullscreen)
        return;
    auto master = std::find_if(clients_.begin(), clients_.end(), [&](const auto &item) {
        return is_visible(item) && !item.floating;
    });
    auto target = clients_.begin() + (selected - clients_.data());
    if (target == master)
        target = std::find_if(target + 1, clients_.end(),
                              [&](const auto &item) { return is_visible(item) && !item.floating; });
    if (target == clients_.end())
        return;
    Window window = target->window;
    std::rotate(clients_.begin(), target, target + 1);
    arrange();
    focus(window);
}
void WindowManager::move_in_stack(int direction) {
    auto *selected = find_client(focused_window_);
    if (!selected || selected->floating || selected->fullscreen)
        return;
    const auto index = static_cast<std::size_t>(selected - clients_.data());
    auto target = index;
    if (direction > 0) {
        for (target = index + 1; target < clients_.size(); ++target)
            if (is_visible(clients_[target]) && !clients_[target].floating)
                break;
        if (target == clients_.size())
            std::rotate(clients_.begin(), clients_.begin() + static_cast<std::ptrdiff_t>(index),
                        clients_.begin() + static_cast<std::ptrdiff_t>(index + 1));
        else
            std::rotate(clients_.begin() + static_cast<std::ptrdiff_t>(index),
                        clients_.begin() + static_cast<std::ptrdiff_t>(index + 1),
                        clients_.begin() + static_cast<std::ptrdiff_t>(target + 1));
    } else {
        bool found = false;
        for (auto previous = index; previous > 0; --previous)
            if (is_visible(clients_[previous - 1]) && !clients_[previous - 1].floating) {
                target = previous - 1;
                found = true;
                break;
            }
        if (found)
            std::rotate(clients_.begin() + static_cast<std::ptrdiff_t>(target),
                        clients_.begin() + static_cast<std::ptrdiff_t>(index),
                        clients_.begin() + static_cast<std::ptrdiff_t>(index + 1));
        else
            std::rotate(clients_.begin() + static_cast<std::ptrdiff_t>(index),
                        clients_.begin() + static_cast<std::ptrdiff_t>(index + 1), clients_.end());
    }
    arrange();
}
void WindowManager::hide(Client &client) {
    client.hidden = true;
    set_client_state(client, IconicState);
    arrange();
}
void WindowManager::show_all() {
    preview_window_ = None;
    for (auto &client : clients_)
        if (on_current_tags(client)) {
            client.hidden = false;
            set_client_state(client, NormalState);
        }
    arrange();
}
void WindowManager::toggle_scratchpad() {
    for (auto &client : clients_)
        if (client.scratchpad) {
            if (is_visible(client)) {
                hide(client);
            } else {
                client.hidden = false;
                set_client_state(client, NormalState);
                assign_tags(client, workspace_.active_tags());
                focus(client.window);
            }
            return;
        }
    spawn(config::scratchpad);
}
void WindowManager::handle_key_press(const XKeyEvent &event) {
    auto keysym = XLookupKeysym(const_cast<XKeyEvent *>(&event), 0);
    auto modifiers = event.state & ~(numlock_mask_ | LockMask);
    auto *client = find_client(focused_window_);
    using config::Action;
    for (const auto &binding : config::keys) {
        if (binding.key != keysym || binding.modifiers != modifiers)
            continue;
        switch (binding.action) {
        case Action::ViewTag:
        case Action::ToggleViewTag:
        case Action::SendTag:
        case Action::ToggleSendTag: {
            if (binding.argument < 1 || binding.argument > 9)
                break;
            const TagMask tags = 1u << static_cast<unsigned>(binding.argument - 1);
            const bool toggle =
                binding.action == Action::ToggleViewTag || binding.action == Action::ToggleSendTag;
            if (binding.action == Action::SendTag || binding.action == Action::ToggleSendTag) {
                if (client)
                    assign_tags(*client, tags, config::view_on_tag && !toggle, toggle);
            } else {
                view_tags(tags, toggle);
            }
            break;
        }
        case Action::Spawn:
            spawn(binding.command);
            break;
        case Action::Focus:
            cycle_focus(binding.argument);
            break;
        case Action::FocusHidden:
            cycle_focus(binding.argument, true);
            break;
        case Action::MasterCount:
            workspace_.settings().master_count =
                std::max(0, workspace_.settings().master_count + binding.argument);
            arrange();
            break;
        case Action::AllTags:
            view_tags(all_tags);
            break;
        case Action::TagAll:
            if (client)
                assign_tags(*client, all_tags, config::view_on_tag);
            break;
        case Action::Restart:
            restart_requested_ = true;
            quit();
            break;
        // A single-screen session has no other monitor to focus or move a window to.
        case Action::FocusMonitor:
        case Action::SendMonitor:
            break;
        case Action::Ratio:
            workspace_.settings().master_ratio = std::clamp(
                workspace_.settings().master_ratio + binding.argument * config::ratio_step, 0.1,
                0.9);
            arrange();
            break;
        case Action::Layout:
            select_layout(static_cast<std::size_t>(binding.argument));
            break;
        case Action::CycleLayout:
            toggle_layout();
            break;
        case Action::Float:
            if (client)
                toggle_floating(*client);
            break;
        case Action::Fullscreen:
            if (client)
                set_fullscreen(*client, !client->fullscreen);
            break;
        case Action::Close:
            if (client)
                close_window(*client);
            break;
        case Action::Quit:
            quit();
            break;
        case Action::Zoom:
            promote_to_master();
            break;
        case Action::PreviousTag:
            view_tags(0);
            break;
        case Action::Bar:
            workspace_.settings().bar_visible = !workspace_.settings().bar_visible;
            if (workspace_.settings().bar_visible)
                XMapRaised(display_, bar_->window());
            else
                XUnmapWindow(display_, bar_->window());
            arrange();
            break;
        case Action::Hide:
            if (client)
                hide(*client);
            break;
        case Action::ShowAll:
            show_all();
            break;
        case Action::Scratch:
            toggle_scratchpad();
            break;
        case Action::Push:
            move_in_stack(binding.argument);
            break;
        }
        return;
    }
}
void WindowManager::handle_button_press(const XButtonEvent &event) {
    using config::MouseAction;
    using config::MouseContext;
    MouseContext context = MouseContext::Client;
    BarHit hit{};
    TagMask tags{};
    auto *client = find_client(event.window);
    if (event.window == bar_->window()) {
        const int tags_width = static_cast<int>(config::tag_labels.size()) * config::tag_width;
        if (event.x >= 0 && event.x < tags_width) {
            context = MouseContext::Tag;
            tags = 1u << static_cast<unsigned>(event.x / config::tag_width);
        } else if (event.x >= tags_width && event.x < tags_width + config::layout_width) {
            context = MouseContext::Layout;
        } else {
            hit = bar_->hit_test(event.x);
            client = find_client(hit.client);
            if (client && on_current_tags(*client))
                context = MouseContext::Title;
            else if (hit.external || hit.module >= 0)
                context = MouseContext::StatusArea;
            else
                return;
        }
    } else {
        if (!client)
            return;
        focus(client->window);
        XAllowEvents(display_, ReplayPointer, event.time);
        if (client->fullscreen || focused_window_ != client->window)
            return;
    }
    const unsigned modifiers = event.state & ~(numlock_mask_ | LockMask);
    for (const auto &binding : config::mouse_bindings) {
        if (binding.context != context || binding.button != event.button ||
            binding.modifiers != modifiers)
            continue;
        switch (binding.action) {
        case MouseAction::Spawn:
            spawn(binding.command);
            break;
        case MouseAction::ModuleCommand:
            if (hit.external) {
                spawn(config::slstatus_click);
                break;
            }
            if (hit.module >= 0 &&
                static_cast<std::size_t>(hit.module) < config::status_modules.size())
                spawn(config::status_modules[static_cast<std::size_t>(hit.module)].on_click);
            break;
        case MouseAction::ViewTag:
        case MouseAction::ToggleViewTag:
            if (tags)
                view_tags(tags, binding.action == MouseAction::ToggleViewTag);
            break;
        case MouseAction::SendTag:
        case MouseAction::ToggleSendTag:
            if (auto *selected = find_client(focused_window_); selected && tags) {
                const bool toggle = binding.action == MouseAction::ToggleSendTag;
                assign_tags(*selected, tags, config::view_on_tag && !toggle, toggle);
            }
            break;
        case MouseAction::CycleLayout:
            toggle_layout();
            break;
        case MouseAction::Layout:
            select_layout(static_cast<std::size_t>(binding.argument));
            break;
        case MouseAction::ToggleHide:
        case MouseAction::FocusZoom:
            if (!client)
                break;
            if (binding.action == MouseAction::ToggleHide && client->window == focused_window_) {
                hide(*client);
            } else {
                if (preview_window_ == client->window)
                    preview_window_ = None;
                client->hidden = false;
                set_client_state(*client, NormalState);
                arrange();
                focus(client->window);
                if (binding.action == MouseAction::FocusZoom)
                    promote_to_master();
            }
            break;
        case MouseAction::Float:
            if (client)
                toggle_floating(*client);
            break;
        case MouseAction::Move:
        case MouseAction::Resize: {
            if (!client || client->fullscreen)
                break;
            if (!client->floating) {
                client->floating_geometry = client->geometry;
                client->floating = true;
                arrange();
            }
            const bool resize = binding.action == MouseAction::Resize;
            if (XGrabPointer(display_, root_window_, False, PointerMotionMask | ButtonReleaseMask,
                             GrabModeAsync, GrabModeAsync, None,
                             resize ? resize_cursor_ : move_cursor_, event.time) == GrabSuccess)
                drag_ = {client->window, event.x_root, event.y_root, client->geometry, resize};
            break;
        }
        }
        return;
    }
}
} // namespace ppwm

// ---- Session ----
namespace ppwm {
namespace {
constexpr unsigned long session_version = 1;
constexpr std::size_t header_words = 8, settings_words = 50, client_words = 16;
constexpr std::size_t max_clients = 4096;
unsigned long encode(int value) {
    return static_cast<std::uint32_t>(value);
}
int decode(unsigned long value) {
    return static_cast<std::int32_t>(static_cast<std::uint32_t>(value));
}
} // namespace

// One temporary X property survives exec without leaving user state on disk.
void WindowManager::save_session() {
    if (clients_.size() > max_clients)
        return;
    const auto workspace = workspace_.snapshot();
    std::vector<unsigned long> data{session_version,
                                    workspace.active,
                                    workspace.previous,
                                    workspace.primary,
                                    workspace.previous_primary,
                                    focused_window_,
                                    preview_window_,
                                    clients_.size()};
    data.reserve(header_words + settings_words + client_words * clients_.size());
    for (const auto &settings : workspace.settings) {
        data.insert(data.end(),
                    {static_cast<unsigned long>(settings.master_ratio * 1000000 + 0.5),
                     static_cast<unsigned long>(settings.master_count), settings.layout_index,
                     settings.previous_layout, settings.bar_visible ? 1UL : 0UL});
    }
    for (const auto &client : clients_) {
        const unsigned long flags = (client.floating ? 1UL : 0UL) |
                                    (client.fullscreen ? 2UL : 0UL) | (client.hidden ? 4UL : 0UL) |
                                    (client.scratchpad ? 8UL : 0UL);
        data.insert(data.end(),
                    {client.window, client.tags, flags, encode(client.original_border),
                     encode(client.geometry.x), encode(client.geometry.y),
                     encode(client.geometry.width), encode(client.geometry.height),
                     encode(client.floating_geometry.x), encode(client.floating_geometry.y),
                     encode(client.floating_geometry.width),
                     encode(client.floating_geometry.height),
                     static_cast<std::uint32_t>(client.focus_order),
                     static_cast<std::uint32_t>(client.focus_order >> 32), client.terminal_window,
                     client.swallowed_by});
    }
    XChangeProperty(display_, root_window_, atom("_PPWM_RESTART_SESSION"), XA_CARDINAL, 32,
                    PropModeReplace, reinterpret_cast<const unsigned char *>(data.data()),
                    static_cast<int>(data.size()));
}

void WindowManager::load_session() {
    Atom type{};
    int format{};
    unsigned long count{}, remaining{};
    unsigned char *raw = nullptr;
    const Atom property = atom("_PPWM_RESTART_SESSION");
    const auto limit = header_words + settings_words + client_words * max_clients;
    const int result =
        XGetWindowProperty(display_, root_window_, property, 0, static_cast<long>(limit), True,
                           XA_CARDINAL, &type, &format, &count, &remaining, &raw);
    XDeleteProperty(display_, root_window_, property);
    if (result == Success && type == XA_CARDINAL && format == 32 && !remaining &&
        count >= header_words + settings_words && raw) {
        const auto *words = reinterpret_cast<unsigned long *>(raw);
        session_data_.assign(words, words + count);
        for (auto &word : session_data_)
            word &= 0xffffffffUL;
        if (session_data_[0] != session_version || session_data_[7] > max_clients ||
            count != header_words + settings_words + client_words * session_data_[7])
            session_data_.clear();
    }
    if (raw)
        XFree(raw);
    restoring_ = !session_data_.empty();
}

void WindowManager::restore_session() {
    restoring_ = false;
    if (session_data_.empty()) {
        if (!workspace_.settings().bar_visible)
            XUnmapWindow(display_, bar_->window());
        return;
    }
    const auto &data = session_data_;
    WorkspaceSnapshot workspace;
    workspace.active = static_cast<TagMask>(data[1]);
    workspace.previous = static_cast<TagMask>(data[2]);
    workspace.primary = static_cast<unsigned>(data[3]);
    workspace.previous_primary = static_cast<unsigned>(data[4]);
    std::size_t offset = header_words;
    for (auto &settings : workspace.settings) {
        settings = {static_cast<double>(data[offset]) / 1000000, decode(data[offset + 1]),
                    data[offset + 2], data[offset + 3], data[offset + 4] != 0};
        offset += 5;
    }
    // Invalid workspace settings leave constructor defaults in place; valid client
    // records can still be restored and receive normal border/state initialization.
    workspace_.restore(workspace, config::layouts.size());
    std::unordered_map<Window, std::size_t> order;
    for (std::size_t i = 0; i < data[7]; ++i, offset += client_words) {
        auto *client = find_client(data[offset]);
        const auto tags = static_cast<TagMask>(data[offset + 1]);
        if (!client || !tags || (tags & ~all_tags))
            continue;
        const Rect geometry{decode(data[offset + 4]), decode(data[offset + 5]),
                            decode(data[offset + 6]), decode(data[offset + 7])};
        const Rect floating{decode(data[offset + 8]), decode(data[offset + 9]),
                            decode(data[offset + 10]), decode(data[offset + 11])};
        if (geometry.width < 1 || geometry.height < 1 || floating.width < 1 || floating.height < 1)
            continue;
        order.emplace(client->window, i);
        client->tags = tags;
        const auto flags = data[offset + 2];
        client->floating = flags & 1;
        client->fullscreen = flags & 2;
        client->hidden = flags & 4;
        client->scratchpad = flags & 8;
        client->original_border = std::clamp(decode(data[offset + 3]), 0, 65535);
        set_geometry(*client, geometry);
        client->floating_geometry = floating;
        client->focus_order =
            data[offset + 12] | (static_cast<std::uint64_t>(data[offset + 13]) << 32);
        focus_serial_ = std::max(focus_serial_, client->focus_order);
        client->terminal_window = data[offset + 14];
        client->swallowed_by = data[offset + 15];
    }
    // Relationships survive only if both clients are still present and agree.
    for (auto &client : clients_) {
        if (auto *child = find_client(client.swallowed_by);
            !child || child->terminal_window != client.window)
            client.swallowed_by = None;
        if (auto *terminal = find_client(client.terminal_window);
            !terminal || terminal->swallowed_by != client.window)
            client.terminal_window = None;
        publish_client_tags(client);
        set_client_state(client, client.hidden ? IconicState : NormalState);
    }
    std::stable_sort(clients_.begin(), clients_.end(), [&](const Client &a, const Client &b) {
        const auto first = order.find(a.window), second = order.find(b.window);
        return (first == order.end() ? max_clients : first->second) <
               (second == order.end() ? max_clients : second->second);
    });
    const Window selected = data[5], preview = data[6];
    session_data_.clear();
    set_cardinal(root_window_, "_NET_CURRENT_DESKTOP", workspace_.primary_tag());
    if (!workspace_.settings().bar_visible)
        XUnmapWindow(display_, bar_->window());
    arrange();
    if (auto *client = find_client(selected); client && is_visible(*client))
        focus(selected);
    if (preview == focused_window_)
        preview_window_ = preview;
}
} // namespace ppwm

// ---- Bar ----
namespace ppwm {
Bar::Bar(Display *display, int screen, int width) : display_(display), screen_number_(screen) {
    auto visual = DefaultVisual(display_, screen_number_);
    auto colormap = DefaultColormap(display_, screen_number_);
    font_ = XftFontOpenName(display_, screen_number_, config::font);
    if (!font_)
        throw std::runtime_error("cannot load bar font");
    fallback_font_ = XftFontOpenName(display_, screen_number_, config::fallback_font);
    XftColorAllocName(display_, visual, colormap, config::foreground, &foreground_);
    XftColorAllocName(display_, visual, colormap, config::background, &background_);
    XftColorAllocName(display_, visual, colormap, config::accent, &selection_);
    XftColorAllocName(display_, visual, colormap, config::bar_muted, &muted_);
    XftColorAllocName(display_, visual, colormap, config::bar_bright, &bright_);
    XftColorAllocName(display_, visual, colormap, config::bar_title_background, &title_background_);
    XftColorAllocName(display_, visual, colormap, config::bar_divider, &divider_);
    XSetWindowAttributes attributes{};
    attributes.override_redirect = True;
    attributes.background_pixel = background_.pixel;
    attributes.event_mask = ButtonPressMask;
    window_ = XCreateWindow(display_, RootWindow(display_, screen_number_), 0, 0,
                            static_cast<unsigned>(width), config::bar_height, 0, CopyFromParent,
                            InputOutput, CopyFromParent,
                            CWOverrideRedirect | CWBackPixel | CWEventMask, &attributes);
    XStoreName(display_, window_, "ppwm-bar");
    copy_gc_ = XCreateGC(display_, window_, 0, nullptr);
    resize(width);
    XMapRaised(display_, window_);
}
Bar::~Bar() {
    if (text_draw_)
        XftDrawDestroy(text_draw_);
    if (buffer_)
        XFreePixmap(display_, buffer_);
    XFreeGC(display_, copy_gc_);
    XDestroyWindow(display_, window_);
    XftFontClose(display_, font_);
    if (fallback_font_)
        XftFontClose(display_, fallback_font_);
    auto v = DefaultVisual(display_, screen_number_);
    auto c = DefaultColormap(display_, screen_number_);
    XftColorFree(display_, v, c, &foreground_);
    XftColorFree(display_, v, c, &background_);
    XftColorFree(display_, v, c, &selection_);
    XftColorFree(display_, v, c, &muted_);
    XftColorFree(display_, v, c, &bright_);
    XftColorFree(display_, v, c, &title_background_);
    XftColorFree(display_, v, c, &divider_);
    for (auto &[name, color] : status_colors_)
        XftColorFree(display_, v, c, &color);
}
void Bar::resize(int width) {
    width_ = std::max(1, width);
    painted_ = false;
    if (text_draw_)
        XftDrawDestroy(text_draw_);
    if (buffer_)
        XFreePixmap(display_, buffer_);
    XResizeWindow(display_, window_, static_cast<unsigned>(width_), config::bar_height);
    buffer_ = XCreatePixmap(display_, window_, static_cast<unsigned>(width_), config::bar_height,
                            static_cast<unsigned>(DefaultDepth(display_, screen_number_)));
    text_draw_ = XftDrawCreate(display_, buffer_, DefaultVisual(display_, screen_number_),
                               DefaultColormap(display_, screen_number_));
    XftDrawRect(text_draw_, &background_, 0, 0, static_cast<unsigned>(width_), config::bar_height);
    // The X server can repaint exposed/remapped areas from the most recent frame.
    XSetWindowBackgroundPixmap(display_, window_, buffer_);
}
void Bar::draw_text(int x, int width, const std::string &value, TextStyle style, int padding) {
    if (width <= 0)
        return;
    XRectangle clip{static_cast<short>(x), 0, static_cast<unsigned short>(std::min(width, 65535)),
                    config::bar_height};
    XftDrawSetClipRectangles(text_draw_, 0, 0, &clip, 1);
    const auto *background = style == TextStyle::Selected ? &selection_
                             : style == TextStyle::Title  ? &title_background_
                                                          : &background_;
    const auto *foreground = style == TextStyle::Selected ? &bright_
                             : style == TextStyle::Hidden ? &muted_
                                                          : &foreground_;
    XftDrawRect(text_draw_, background, x, 0, static_cast<unsigned>(width), config::bar_height);
    if (padding < 0)
        padding = config::text_padding;
    std::string label = value;
    const int available = std::max(0, width - 2 * padding);
    int label_width = text_width(label);
    if (label_width > available) {
        const std::string ellipsis = "…";
        const int suffix_width = text_width(ellipsis);
        if (suffix_width > available)
            label.clear();
        else {
            while (!label.empty() && text_width(label) + suffix_width > available) {
                auto last = label.size() - 1;
                while (last > 0 && (static_cast<unsigned char>(label[last]) & 0xc0) == 0x80)
                    --last;
                label.resize(last);
            }
            label += ellipsis;
        }
        label_width = text_width(label);
    }
    const int offset = config::center_bar_text ? (width - label_width) / 2 : padding;
    text_runs(label, foreground, x + offset);
    XftDrawSetClip(text_draw_, nullptr);
}
BarHit Bar::hit_test(int x) const {
    for (const auto &region : regions_)
        if (x >= region.left && x < region.right)
            return region.hit;
    return {};
}
void Bar::render(const BarState &state, int reserved_width) {
    const auto now = std::chrono::steady_clock::now();
    const bool refresh_status = now >= next_status_;
    if (refresh_status) {
        read_external_status();
        status_parts_.resize(config::status_modules.size());
        status_widths_.resize(config::status_modules.size());
        for (std::size_t i = 0; !external_status_ && i < status_parts_.size(); ++i) {
            status_parts_[i] = config::status_modules[i].read(state);
            status_widths_[i] = status_parts_[i].empty()
                                    ? 0
                                    : text_width(status_parts_[i]) + 2 * config::text_padding + 2;
        }
        next_status_ = now + config::status_interval;
    }
    // The UI is sampled by a timer; unchanged ticks make no X11 drawing calls.
    if (painted_ && !refresh_status && reserved_width == last_reserved_ && state == last_state_)
        return;
    last_state_ = state;
    last_reserved_ = reserved_width;
    painted_ = true;
    const int content_width = std::max(0, width_ - reserved_width);
    XftDrawRect(text_draw_, &background_, 0, 0, static_cast<unsigned>(width_), config::bar_height);
    for (unsigned i = 0; i < 9; ++i) {
        int x = static_cast<int>(i) * config::tag_width;
        const bool empty = !((state.active_tags | state.occupied) & (1u << i));
        const auto label = empty && !config::empty_tag_label.empty() ? config::empty_tag_label
                                                                     : config::tag_labels[i];
        draw_text(x, config::tag_width, std::string(label),
                  (state.active_tags & (1u << i)) ? TextStyle::Selected
                  : (state.occupied & (1u << i))  ? TextStyle::Normal
                                                  : TextStyle::Hidden,
                  config::tag_padding);
    }
    int left = 9 * config::tag_width;
    draw_text(left, config::layout_width, state.floating ? "f" : state.layout);
    left += config::layout_width;
    regions_.clear();
    const auto &parts = status_parts_;
    const auto &widths = status_widths_;
    int status_width = 0;
    for (int width : widths)
        status_width += width;
    if (external_status_)
        status_width = external_width_;
    status_width = std::min(std::max(0, content_width - left), status_width);
    const int title_width = std::max(0, content_width - left - status_width);
    const int count = static_cast<int>(state.clients.size());
    for (int i = 0; i < count; ++i) {
        const auto &client = state.clients[static_cast<std::size_t>(i)];
        const int start = left + title_width * i / count;
        const int end = left + title_width * (i + 1) / count;
        draw_text(start, end - start, client.hidden ? "[" + client.title + "]" : client.title,
                  client.selected ? TextStyle::Selected
                  : client.hidden ? TextStyle::Hidden
                                  : TextStyle::Title);
        regions_.push_back({start, end, {client.window, -1}});
    }
    int position = content_width - status_width;
    if (external_status_) {
        draw_external_status(position, status_width);
        regions_.push_back({position, content_width, {None, -1, true}});
    }
    for (std::size_t i = 0; !external_status_ && i < parts.size(); ++i) {
        const int width = std::min(widths[i], content_width - position);
        draw_text(position, width, parts[i]);
        if (width > 0)
            XftDrawRect(text_draw_, &divider_, position, 6, 1, config::bar_height - 12);
        regions_.push_back({position, position + width, {None, static_cast<int>(i)}});
        position += width;
    }
    XCopyArea(display_, buffer_, window_, copy_gc_, 0, 0, static_cast<unsigned>(width_),
              config::bar_height, 0, 0);
    XFlush(display_);
}
} // namespace ppwm

// ---- Bar Status ----
namespace ppwm {
// Measure and draw identical font runs so fallback glyphs keep alignment and clipping.
int Bar::text_runs(const std::string &text, const XftColor *color, int x) const {
    int advance = 0;
    std::size_t start = 0, position = 0;
    XftFont *run_font = font_;
    auto flush = [&](std::size_t end) {
        if (end == start)
            return;
        const auto *bytes = reinterpret_cast<const FcChar8 *>(text.data() + start);
        const int length = static_cast<int>(end - start);
        XGlyphInfo extent{};
        XftTextExtentsUtf8(display_, run_font, bytes, length, &extent);
        if (color)
            XftDrawStringUtf8(text_draw_, color, run_font, x + advance,
                              (config::bar_height - run_font->ascent - run_font->descent) / 2 +
                                  run_font->ascent,
                              bytes, length);
        advance += extent.xOff;
        start = end;
    };
    while (position < text.size()) {
        FcChar32 codepoint{};
        const int decoded = FcUtf8ToUcs4(reinterpret_cast<const FcChar8 *>(text.data() + position),
                                         &codepoint, static_cast<int>(text.size() - position));
        auto *chosen = font_;
        if (decoded > 0 && codepoint >= 128 && fallback_font_ &&
            !XftCharExists(display_, font_, codepoint) &&
            XftCharExists(display_, fallback_font_, codepoint))
            chosen = fallback_font_;
        if (chosen != run_font) {
            flush(position);
            run_font = chosen;
        }
        position += static_cast<std::size_t>(std::max(1, decoded));
    }
    flush(position);
    return advance;
}
int Bar::text_width(const std::string &text) const {
    return text_runs(text);
}
void Bar::read_external_status() {
    external_status_ = false;
    external_parts_.clear();
    if (!config::use_slstatus)
        return;
    Atom type{};
    int format{};
    unsigned long count{}, remaining{};
    unsigned char *raw = nullptr;
    if (XGetWindowProperty(display_, RootWindow(display_, screen_number_), XA_WM_NAME, 0, 1024,
                           False, AnyPropertyType, &type, &format, &count, &remaining,
                           &raw) == Success &&
        raw && format == 8 && count) {
        external_parts_ = parse_status(std::string_view(reinterpret_cast<char *>(raw), count));
    }
    if (raw)
        XFree(raw);
    int cursor = 0;
    bool visible = false;
    for (const auto &part : external_parts_) {
        if (part.kind == StatusKind::Text) {
            cursor += text_width(part.text);
            visible |= !part.text.empty();
        }
        if (part.kind == StatusKind::Offset)
            cursor += part.x;
        if (part.kind == StatusKind::Rectangle)
            visible = true;
        cursor = std::clamp(cursor, 0, 16384);
    }
    external_status_ = visible;
    external_width_ = cursor + 2 * config::text_padding;
}
void Bar::draw_external_status(int x, int width) {
    if (width <= 0)
        return;
    XRectangle clip{static_cast<short>(x), 0, static_cast<unsigned short>(std::min(width, 65535)),
                    config::bar_height};
    XftDrawSetClipRectangles(text_draw_, 0, 0, &clip, 1);
    const XftColor *foreground = &foreground_, *background = &background_;
    auto color = [&](const std::string &name, const XftColor *fallback) -> const XftColor * {
        if (auto found = status_colors_.find(name); found != status_colors_.end())
            return &found->second;
        if (status_colors_.size() >= 64)
            return fallback;
        XftColor allocated{};
        if (!XftColorAllocName(display_, DefaultVisual(display_, screen_number_),
                               DefaultColormap(display_, screen_number_), name.c_str(), &allocated))
            return fallback;
        return &status_colors_.emplace(name, allocated).first->second;
    };
    int cursor = 0;
    for (const auto &part : external_parts_) {
        switch (part.kind) {
        case StatusKind::Foreground:
            foreground = color(part.text, &foreground_);
            break;
        case StatusKind::Background:
            background = color(part.text, &background_);
            break;
        case StatusKind::Reset:
            foreground = &foreground_;
            background = &background_;
            break;
        case StatusKind::Offset:
            cursor = std::clamp(cursor + part.x, 0, 16384);
            break;
        case StatusKind::Rectangle:
            XftDrawRect(text_draw_, foreground, x + config::text_padding + cursor + part.x, part.y,
                        static_cast<unsigned>(part.width), static_cast<unsigned>(part.height));
            break;
        case StatusKind::Text: {
            const int advance = text_width(part.text);
            XftDrawRect(text_draw_, background, x + config::text_padding + cursor, 0,
                        static_cast<unsigned>(advance), config::bar_height);
            text_runs(part.text, foreground, x + config::text_padding + cursor);
            cursor = std::clamp(cursor + advance, 0, 16384);
            break;
        }
        }
    }
    XftDrawSetClip(text_draw_, nullptr);
}
} // namespace ppwm

// ---- Bar Modules ----
namespace ppwm::bar_modules {
std::string battery(const BarState &) {
    std::ifstream file(config::battery_path);
    int percent = 0;
    return file >> percent ? "BAT " + std::to_string(percent) + "%" : "BAT n/a";
}
std::string clock_module(const BarState &) {
    char b[128]{};
    auto now = std::time(nullptr);
    std::tm t{};
    localtime_r(&now, &t);
    std::strftime(b, sizeof b, config::clock_format, &t);
    return b;
}
std::string keep_awake(const BarState &) {
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
} // namespace ppwm::bar_modules

// ---- System Tray ----
namespace ppwm {
namespace {
constexpr long request_dock = 0;
constexpr long embedded_notify = 0;
constexpr long window_activate = 1;
constexpr long mapped_flag = 1;
} // namespace

SystemTray::SystemTray(Display *display, int screen, Window bar, int bar_width)
    : display_(display), root_(RootWindow(display, screen)), bar_(bar), screen_(screen),
      bar_width_(bar_width) {
    const auto name = "_NET_SYSTEM_TRAY_S" + std::to_string(screen);
    selection_ = XInternAtom(display_, name.c_str(), False);
    opcode_ = XInternAtom(display_, "_NET_SYSTEM_TRAY_OPCODE", False);
    xembed_ = XInternAtom(display_, "_XEMBED", False);
    xembed_info_ = XInternAtom(display_, "_XEMBED_INFO", False);
    refresh();
}

void SystemTray::refresh() {
    const auto now = std::chrono::steady_clock::now();
    if (!config::tray_enabled || window_ || now < next_attempt_)
        return;
    next_attempt_ = now + config::tray_retry_interval;
    if (XGetSelectionOwner(display_, selection_) != None)
        return;

    window_ = XCreateSimpleWindow(display_, bar_, 0, 0, 1, config::bar_height, 0, 0, 0);
    XStoreName(display_, window_, "ppwm-tray");
    XSelectInput(display_, window_, SubstructureRedirectMask | SubstructureNotifyMask);
    const auto orientation = XInternAtom(display_, "_NET_SYSTEM_TRAY_ORIENTATION", False);
    const auto visual_property = XInternAtom(display_, "_NET_SYSTEM_TRAY_VISUAL", False);
    unsigned long horizontal = 0;
    VisualID visual = XVisualIDFromVisual(DefaultVisual(display_, screen_));
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

// ---- Process ----
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
        const auto [end, error] =
            std::from_chars(value.data(), value.data() + value.size(), parent);
        if (error == std::errc{} && end == value.data() + value.size() && parent > 0)
            return parent;
        return std::nullopt;
    }
    return std::nullopt;
}
} // namespace

ProcessLookup::ProcessLookup(Display *display) : display_(display) {
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
    XResClientIdValue *values = nullptr;
    const auto result = XResQueryClientIds(display_, 1, &spec, &count, &values);
    const auto destroy = [count](XResClientIdValue *ids) { XResClientIdsDestroy(count, ids); };
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
