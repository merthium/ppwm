#include "bar_modules.hpp"
#include "config.hpp"
#include "keys.hpp"
#include "window_manager.hpp"
#include <algorithm>
#include <cstdlib>
#include <unistd.h>
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
        for (unsigned button : {Button1, Button2, Button3})
            XGrabButton(display_, button, config::modifier | lock, client.window, False,
                        ButtonPressMask | ButtonReleaseMask | PointerMotionMask, GrabModeAsync,
                        GrabModeAsync, None, None);
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
    for (KeySym keysym = XK_1; keysym <= XK_9; ++keysym) {
        for (unsigned extra :
             {0u, static_cast<unsigned>(ShiftMask), static_cast<unsigned>(ControlMask),
              static_cast<unsigned>(ControlMask | ShiftMask)})
            grab(keysym, config::modifier | extra);
    }
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
    if (keysym >= XK_1 && keysym <= XK_9 && (modifiers & config::modifier) &&
        !(modifiers & ~(config::modifier | ShiftMask | ControlMask))) {
        const TagMask tags = 1u << static_cast<unsigned>(keysym - XK_1);
        const bool toggle = modifiers & ControlMask;
        if ((modifiers & ShiftMask) && client)
            assign_tags(*client, tags, config::view_on_tag && !toggle, toggle);
        else
            view_tags(tags, toggle);
        return;
    }
    using config::Action;
    for (const auto &binding : config::keys) {
        if (binding.key != keysym || binding.modifiers != modifiers)
            continue;
        switch (binding.action) {
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
    if (event.window == bar_->window()) {
        if (event.x < 9 * config::tag_width &&
            (event.button == Button1 || event.button == Button3)) {
            TagMask tags = 1u << static_cast<unsigned>(event.x / config::tag_width);
            bool toggle = event.button == Button3;
            if ((event.state & config::modifier) && find_client(focused_window_))
                assign_tags(*find_client(focused_window_), tags, config::view_on_tag && !toggle,
                            toggle);
            else
                view_tags(tags, toggle);
        } else if (event.x < 9 * config::tag_width + 50) {
            if (event.button == Button1)
                toggle_layout();
            else if (event.button == Button3)
                select_layout(2);
        } else {
            const auto hit = bar_->hit_test(event.x);
            if (auto *target = find_client(hit.client); target && on_current_tags(*target)) {
                if (event.button == Button1 && target->window == focused_window_)
                    hide(*target);
                else if (event.button == Button1 || event.button == Button2) {
                    preview_window_ = None;
                    target->hidden = false;
                    set_client_state(*target, NormalState);
                    arrange();
                    focus(target->window);
                    if (event.button == Button2)
                        promote_to_master();
                }
            } else if (hit.module >= 0) {
                if (event.button == Button2)
                    spawn(config::terminal);
                else if (event.button == Button1 || event.button == Button3)
                    spawn(bar_modules::modules[static_cast<std::size_t>(hit.module)].on_click);
            }
        }
        return;
    }
    auto *client = find_client(event.window);
    if (!client)
        return;
    focus(client->window);
    XAllowEvents(display_, ReplayPointer, event.time);
    if (!(event.state & config::modifier) || client->fullscreen ||
        focused_window_ != client->window)
        return;
    if (event.button == Button2) {
        toggle_floating(*client);
        return;
    }
    if (event.button != Button1 && event.button != Button3)
        return;
    if (!client->floating) {
        client->floating_geometry = client->geometry;
        client->floating = true;
        arrange();
    }
    if (XGrabPointer(display_, root_window_, False, PointerMotionMask | ButtonReleaseMask,
                     GrabModeAsync, GrabModeAsync, None,
                     event.button == Button3 ? resize_cursor_ : move_cursor_,
                     event.time) != GrabSuccess)
        return;
    drag_ = {client->window, event.x_root, event.y_root, client->geometry, event.button == Button3};
}
} // namespace ppwm
