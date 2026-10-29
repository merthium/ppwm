#include "config.hpp"
#include "window_manager.hpp"
#include <X11/Xatom.h>
#include <algorithm>
#include <cstdint>
#include <limits>
#include <unordered_map>

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
    workspace_.restore(workspace, layouts.size());
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
        XSetWindowBorderWidth(display_, client->window, client->fullscreen ? 0 : config::border);
    }
    // Relationships survive only if both clients are still present and agree.
    for (auto &client : clients_) {
        if (auto *child = find_client(client.swallowed_by);
            !child || child->terminal_window != client.window)
            client.swallowed_by = None;
        if (auto *terminal = find_client(client.terminal_window);
            !terminal || terminal->swallowed_by != client.window)
            client.terminal_window = None;
        XSetWindowBorderWidth(display_, client.window, client.fullscreen ? 0 : config::border);
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
