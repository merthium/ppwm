#include "config.hpp"
#include "window_manager.hpp"
#include <X11/Xatom.h>
#include <algorithm>
#include <cstdlib>

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

    const bool floating_layout = layouts[workspace_.settings().layout_index].arrange == nullptr;
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
    set_geometry(*client, constrain_floating(geometry, workarea(), config::border));
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
        geometry.width = std::max(40, geometry.width + dx);
        geometry.height = std::max(40, geometry.height + dy);
    } else {
        geometry.x += dx;
        geometry.y += dy;
        const auto area = workarea();
        const int right = area.x + area.width - geometry.width - 2 * config::border;
        const int bottom = area.y + area.height - geometry.height - 2 * config::border;
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

    if (message.message_type == atom("_NET_WM_DESKTOP") && valid_tag) {
        assign_tag(*client, static_cast<unsigned>(argument));
    } else if (message.message_type == atom("_NET_ACTIVE_WINDOW")) {
        client->hidden = false;
        set_client_state(*client, NormalState);
        view_tags(client->tags);
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
        if (event.xcrossing.mode == NotifyNormal && event.xcrossing.detail != NotifyInferior &&
            !drag_.window && find_client(event.xcrossing.window))
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
