#include "config.hpp"
#include "window_manager.hpp"
#include <X11/Xatom.h>
#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <cstring>
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
    XSetWindowBorderWidth(display_, window, config::border);
    XSetWindowBorder(display_, window, normal_border_);
    for (unsigned mask :
         {0u, static_cast<unsigned>(LockMask), numlock_mask_, numlock_mask_ | LockMask}) {
        for (unsigned b : {Button1, Button2, Button3})
            XGrabButton(display_, b, config::modifier | mask, window, False,
                        ButtonPressMask | ButtonReleaseMask | PointerMotionMask, GrabModeAsync,
                        GrabModeAsync, None, None);
    }
    clients_.insert(clients_.begin(), client);
    auto &added = clients_.front();
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
    event.xconfigure.border_width = client.fullscreen ? 0 : config::border;
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
        if (!client.mapped) {
            XMapWindow(display_, client.window);
            client.mapped = true;
        }
        if (client.fullscreen) {
            set_geometry(client, {0, 0, screen_width_, screen_height_});
            continue;
        }
        if (client.floating || !layouts[workspace_.settings().layout_index].arrange) {
            set_geometry(client,
                         constrain_floating(client.floating_geometry, area, config::border));
        } else
            tiled.push_back(&client);
    }
    if (layouts[workspace_.settings().layout_index].arrange) {
        auto rects = layouts[workspace_.settings().layout_index].arrange(
            area, static_cast<int>(tiled.size()), workspace_.settings().master_ratio, config::gap,
            workspace_.settings().master_count);
        for (std::size_t i = 0; i < tiled.size(); ++i) {
            auto geometry = rects[i];
            geometry.width -= 2 * config::border;
            geometry.height -= 2 * config::border;
            set_geometry(*tiled[i], geometry);
        }
    }
    auto *selected = find_client(focused_window_);
    if (!selected || !is_visible(*selected)) {
        Window next = None;
        for (auto &client : clients_)
            if (is_visible(client)) {
                next = client.window;
                break;
            }
        focus(next);
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
        layouts[workspace_.settings().layout_index].arrange == monocle)
        XRaiseWindow(display_, selected->window);
    for (auto &client : clients_)
        if (is_visible(client) &&
            (client.floating || !layouts[workspace_.settings().layout_index].arrange))
            XRaiseWindow(display_, client.window);
    if (selected && is_visible(*selected) &&
        (selected->floating || !layouts[workspace_.settings().layout_index].arrange))
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
    if (auto *old = find_client(focused_window_))
        XSetWindowBorder(display_, old->window, normal_border_);
    focused_window_ = next ? window : None;
    if (!next) {
        XSetInputFocus(display_, root_window_, RevertToPointerRoot, CurrentTime);
        XDeleteProperty(display_, root_window_, atom("_NET_ACTIVE_WINDOW"));
        return;
    }
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
    workspace_.select_layout(index % layouts.size());
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
    Atom full = atom("_NET_WM_STATE_FULLSCREEN");
    XChangeProperty(display_, client.window, atom("_NET_WM_STATE"), XA_ATOM, 32, PropModeReplace,
                    reinterpret_cast<unsigned char *>(&full), enabled ? 1 : 0);
    XSetWindowBorderWidth(display_, client.window, enabled ? 0 : config::border);
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
