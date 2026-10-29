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
            else if (hit.module >= 0)
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
