#pragma once
#include <X11/Xft/Xft.h>
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <algorithm>
#include <array>
#include <bit>
#include <charconv>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <sys/types.h>
#include <unordered_map>
#include <utility>
#include <vector>

// ---- Config Types ----
namespace ppwm {
struct BarState;
struct BarModule {
    std::string (*read)(const BarState &);
    const char *on_click{};
};
namespace bar_modules {
std::string battery(const BarState &);
std::string clock_module(const BarState &);
std::string keep_awake(const BarState &);
} // namespace bar_modules
} // namespace ppwm

// ---- Keys ----
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

// ---- Layout ----
namespace ppwm {
struct Rect {
    int x{}, y{}, width{}, height{};
    bool operator==(const Rect &) const = default;
};

// Floating geometry is the client area; tiled rectangles include the border.
inline Rect constrain_floating(Rect geometry, Rect area, int border) {
    geometry.width = std::clamp(geometry.width, 1, std::max(1, area.width - 2 * border));
    geometry.height = std::clamp(geometry.height, 1, std::max(1, area.height - 2 * border));
    const int right = std::max(area.x, area.x + area.width - geometry.width - 2 * border);
    const int bottom = std::max(area.y, area.y + area.height - geometry.height - 2 * border);
    geometry.x = std::clamp(geometry.x, area.x, right);
    geometry.y = std::clamp(geometry.y, area.y, bottom);
    return geometry;
}

using LayoutFn = std::vector<Rect> (*)(Rect area, int count, double ratio, int gap, int masters);

inline std::vector<Rect> monocle(Rect area, int count, double, int gap, int = 1) {
    gap = std::clamp(gap, 0, std::max(0, (std::min(area.width, area.height) - 1) / 2));
    const Rect rectangle{area.x + gap, area.y + gap, std::max(1, area.width - 2 * gap),
                         std::max(1, area.height - 2 * gap)};
    return std::vector<Rect>(std::max(0, count), rectangle);
}

inline std::vector<Rect> tile(Rect area, int count, double ratio, int gap, int masters = 1) {
    if (count <= 0)
        return {};
    if (count == 1)
        return monocle(area, count, ratio, gap);
    masters = std::clamp(masters, 0, count);
    const int stack_count = count - masters;
    const bool split = masters > 0 && stack_count > 0;
    const int maximum_gap = std::min((area.width - (split ? 2 : 1)) / (split ? 3 : 2),
                                     (area.height - count) / (count + 1));
    gap = std::clamp(gap, 0, std::max(0, maximum_gap));
    area = {area.x + gap, area.y + gap, std::max(1, area.width - 2 * gap),
            std::max(1, area.height - 2 * gap)};
    const int master_width =
        split ? std::clamp(static_cast<int>((area.width - gap) * std::clamp(ratio, 0.1, 0.9)), 1,
                           std::max(1, area.width - gap - 1))
              : area.width;
    std::vector<Rect> rectangles;
    rectangles.reserve(count);
    auto column = [&](int x, int width, int windows) {
        if (windows == 0)
            return;
        const int usable = std::max(windows, area.height - gap * (windows - 1));
        for (int index = 0; index < windows; ++index) {
            // Integer boundaries distribute leftover pixels without accumulating rounding error.
            int top = usable * index / windows, bottom = usable * (index + 1) / windows;
            rectangles.push_back(
                {x, area.y + top + gap * index, std::max(1, width), std::max(1, bottom - top)});
        }
    };
    column(area.x, master_width, masters);
    column(split ? area.x + master_width + gap : area.x,
           split ? area.width - master_width - gap : area.width, stack_count);
    return rectangles;
}

struct Layout {
    const char *symbol;
    LayoutFn arrange; // nullptr selects floating layout.
};

} // namespace ppwm

// ---- Workspace ----
namespace ppwm {
using TagMask = unsigned;
inline constexpr TagMask all_tags = (1u << 9) - 1;
constexpr unsigned first_tag(TagMask tags) {
    return tags ? std::countr_zero(tags) : 0;
}

struct WorkspaceSettings {
    double master_ratio{0.55};
    int master_count{1};
    std::size_t layout_index{}, previous_layout{1};
    bool bar_visible{true};
};

struct WorkspaceSnapshot {
    TagMask active{1}, previous{1};
    unsigned primary{}, previous_primary{};
    std::array<WorkspaceSettings, 10> settings{};
};

// The tenth slot stores the all-tags view; normal tags retain their own settings.
class Workspace {
  public:
    explicit Workspace(WorkspaceSettings defaults = {}) {
        settings_.fill(defaults);
    }
    [[nodiscard]] TagMask active_tags() const noexcept {
        return active_;
    }
    [[nodiscard]] unsigned primary_tag() const noexcept {
        return primary_ == 9 ? 0 : primary_;
    }
    WorkspaceSettings &settings() noexcept {
        return settings_[primary_];
    }
    const WorkspaceSettings &settings() const noexcept {
        return settings_[primary_];
    }

    WorkspaceSnapshot snapshot() const {
        return {active_, previous_, primary_, previous_primary_, settings_};
    }
    bool restore(const WorkspaceSnapshot &saved, std::size_t layout_count) {
        auto valid_view = [](TagMask mask, unsigned primary) {
            return mask && !(mask & ~all_tags) && primary < 10 &&
                   (primary == 9 ? mask == all_tags : (mask & (1u << primary)) != 0);
        };
        if (!valid_view(saved.active, saved.primary) ||
            !valid_view(saved.previous, saved.previous_primary))
            return false;
        for (const auto &settings : saved.settings)
            if (!(settings.master_ratio >= 0.1 && settings.master_ratio <= 0.9) ||
                settings.master_count < 0 || settings.layout_index >= layout_count ||
                settings.previous_layout >= layout_count)
                return false;
        active_ = saved.active;
        previous_ = saved.previous;
        primary_ = saved.primary;
        previous_primary_ = saved.previous_primary;
        settings_ = saved.settings;
        return true;
    }
    bool view(TagMask tags) {
        tags &= all_tags;
        if (tags == active_)
            return false;
        if (tags == 0) {
            std::swap(active_, previous_);
            std::swap(primary_, previous_primary_);
        } else {
            previous_ = active_;
            previous_primary_ = primary_;
            active_ = tags;
            primary_ = tags == all_tags ? 9 : first_tag(tags);
        }
        return true;
    }
    bool toggle(TagMask tags) {
        const TagMask next = active_ ^ (tags & all_tags);
        if (next == 0)
            return false;
        active_ = next;
        if (next == all_tags)
            primary_ = 9;
        else if (primary_ == 9 || !(next & (1u << primary_)))
            primary_ = first_tag(next);
        return true;
    }
    void select_layout(std::size_t index) {
        auto &current = settings();
        if (index != current.layout_index) {
            current.previous_layout = current.layout_index;
            current.layout_index = index;
        }
    }
    void toggle_layout() {
        auto &current = settings();
        std::swap(current.layout_index, current.previous_layout);
    }

  private:
    TagMask active_{1}, previous_{1};
    unsigned primary_{}, previous_primary_{};
    std::array<WorkspaceSettings, 10> settings_{};
};
} // namespace ppwm

// ---- Status Text ----
namespace ppwm {
enum class StatusKind { Text, Foreground, Background, Reset, Offset, Rectangle };
struct StatusPart {
    StatusKind kind;
    std::string text;
    int x{}, y{}, width{}, height{};
};
inline std::vector<StatusPart> parse_status(std::string_view input) {
    input = input.substr(0, 4096);
    std::vector<StatusPart> parts;
    auto text = [&](char c) {
        if (static_cast<unsigned char>(c) < 32 || c == 127)
            return;
        if (parts.empty() || parts.back().kind != StatusKind::Text)
            parts.push_back({StatusKind::Text, {}});
        parts.back().text += c;
    };
    for (std::size_t i = 0; i < input.size();) {
        if (input[i] != '^') {
            text(input[i++]);
            continue;
        }
        const auto end = input.find('^', i + 1);
        if (end == std::string_view::npos) {
            text(input[i++]);
            continue;
        }
        if (end == i + 1) {
            text('^');
            i = end + 1;
            continue;
        }
        auto command = input.substr(i + 1, end - i - 1);
        std::size_t at = 0;
        auto number = [&](int &value) {
            const auto *start = command.data() + at;
            const auto result = std::from_chars(start, command.data() + command.size(), value);
            if (result.ec != std::errc{})
                return false;
            at = static_cast<std::size_t>(result.ptr - command.data());
            value = std::clamp(value, -4096, 4096);
            return true;
        };
        while (at < command.size()) {
            const char op = command[at++];
            if (op == 'd')
                parts.push_back({StatusKind::Reset, {}});
            else if ((op == 'c' || op == 'b') && command.size() - at >= 7 && command[at] == '#') {
                const auto color = command.substr(at, 7);
                const bool valid = std::all_of(color.begin() + 1, color.end(), [](char c) {
                    return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') ||
                           (c >= 'A' && c <= 'F');
                });
                if (valid)
                    parts.push_back({op == 'c' ? StatusKind::Foreground : StatusKind::Background,
                                     std::string(color)});
                at += 7;
            } else if (op == 'f') {
                int width{};
                if (number(width))
                    parts.push_back({StatusKind::Offset, {}, width});
            } else if (op == 'r') {
                int values[4]{};
                bool valid = true;
                for (int n = 0; n < 4; ++n) {
                    if (!number(values[n])) {
                        valid = false;
                        break;
                    }
                    if (n < 3 && (at == command.size() || command[at++] != ',')) {
                        valid = false;
                        break;
                    }
                }
                if (valid && values[2] > 0 && values[3] > 0)
                    parts.push_back(
                        {StatusKind::Rectangle, {}, values[0], values[1], values[2], values[3]});
            }
        }
        i = end + 1;
    }
    return parts;
}
} // namespace ppwm

// ---- X11 Connection ----
namespace ppwm {
// RAII: the connection closes even if WindowManager construction throws.
class X11Connection {
  public:
    X11Connection() : display_(XOpenDisplay(nullptr)) {
        if (!display_) {
            throw std::runtime_error("cannot open DISPLAY");
        }
    }
    ~X11Connection() {
        XCloseDisplay(display_);
    }

    X11Connection(const X11Connection &) = delete;
    X11Connection &operator=(const X11Connection &) = delete;

    [[nodiscard]] Display *get() const noexcept {
        return display_;
    }

  private:
    Display *display_;
};
} // namespace ppwm

// ---- Process ----
namespace ppwm {
namespace detail {
std::optional<Window> parse_window_id(std::string_view value);
}

class ProcessLookup {
  public:
    explicit ProcessLookup(Display *display);
    std::optional<pid_t> window_pid(Window window) const;
    std::optional<Window> terminal_window(pid_t pid) const;

  private:
    Display *display_;
    bool xres_supported_ = false;
};
} // namespace ppwm

// ---- System Tray ----
namespace ppwm {
class SystemTray {
  public:
    SystemTray(Display *display, int screen, Window bar, int bar_width);
    ~SystemTray();
    SystemTray(const SystemTray &) = delete;
    SystemTray &operator=(const SystemTray &) = delete;

    [[nodiscard]] Window requested_icon(const XEvent &event) const;
    [[nodiscard]] int width() const noexcept {
        return width_;
    }
    bool handle_event(const XEvent &event);
    void resize(int bar_width);
    void refresh();

  private:
    struct Icon {
        Window window;
        bool mapped;
    };
    Display *display_;
    Window root_, bar_, window_{};
    Atom selection_, opcode_, xembed_, xembed_info_;
    int screen_, bar_width_, width_{};
    std::chrono::steady_clock::time_point next_attempt_{};
    std::vector<Icon> icons_;

    Icon *find_icon(Window window);
    void dock(Window window);
    void update_mapped(Icon &icon);
    void remove(Window window, bool destroyed);
    void layout();
    void release();
    void send_embed(Window icon, long message, long data1 = 0);
};
} // namespace ppwm

// ---- Bar ----
namespace ppwm {
struct BarClient {
    Window window{};
    std::string title;
    bool selected{}, hidden{};
    bool operator==(const BarClient &) const = default;
};
struct BarHit {
    Window client{};
    int module{-1};
    bool external{};
};
struct BarState {
    unsigned active_tags{1}, occupied{};
    std::string layout;
    std::vector<BarClient> clients;
    bool floating{};
    bool operator==(const BarState &) const = default;
};
class Bar {
    Display *display_;
    int screen_number_;
    Window window_;
    Pixmap buffer_{};
    GC copy_gc_{};
    XftDraw *text_draw_{};
    XftFont *font_{}, *fallback_font_{};
    XftColor foreground_{}, background_{}, selection_{};
    XftColor muted_{}, bright_{}, title_background_{}, divider_{};
    enum class TextStyle { Normal, Selected, Title, Hidden };
    bool external_status_{};
    std::vector<StatusPart> external_parts_;
    int external_width_{};
    std::unordered_map<std::string, XftColor> status_colors_;
    void read_external_status();
    void draw_external_status(int x, int width);
    int text_runs(const std::string &, const XftColor *color = nullptr, int x = 0) const;
    int text_width(const std::string &) const;
    int width_{};
    BarState last_state_;
    bool painted_{};
    int last_reserved_{};
    std::chrono::steady_clock::time_point next_status_{};
    std::vector<std::string> status_parts_;
    std::vector<int> status_widths_;
    struct HitRegion {
        int left, right;
        BarHit hit;
    };
    std::vector<HitRegion> regions_;
    void draw_text(int x, int width, const std::string &value, TextStyle style = TextStyle::Normal,
                   int padding = -1);

  public:
    Bar(Display *, int, int);
    ~Bar();
    Bar(const Bar &) = delete;
    Bar &operator=(const Bar &) = delete;
    Window window() const {
        return window_;
    }
    BarHit hit_test(int x) const;
    void resize(int);
    void render(const BarState &, int reserved_width = 0);
};
} // namespace ppwm

// ---- Window Manager ----
namespace ppwm {
struct Client {
    Window window{};
    std::string title;
    TagMask tags{1};
    std::uint64_t focus_order{};
    bool terminal{};
    Window swallowed_by{}, terminal_window{};
    bool floating{}, fullscreen{}, mapped{}, input{true}, hidden{}, scratchpad{};
    int ignore_unmap{}, original_border{}, border_width{};
    Rect geometry{}, floating_geometry{};
};
class WindowManager {
    X11Connection connection_;
    Display *display_{connection_.get()}; // Non-owning Xlib handle.
    ProcessLookup processes_{display_};
    int screen_number_{}, screen_width_{}, screen_height_{};
    Window root_window_{}, support_window_{}, focused_window_{};
    Cursor normal_cursor_{}, move_cursor_{}, resize_cursor_{};
    unsigned numlock_mask_{};
    std::uint64_t focus_serial_{};
    Workspace workspace_;
    bool restart_requested_{}, restoring_{};
    std::vector<unsigned long> session_data_;
    void save_session();
    void load_session();
    void restore_session();
    Window preview_window_{};
    unsigned long normal_border_{}, focused_border_{};
    std::vector<Client> clients_;
    std::unordered_map<std::string, Atom> atoms_;
    std::unique_ptr<Bar> bar_;
    std::unique_ptr<SystemTray> tray_;
    struct Drag {
        Window window{};
        int x{}, y{};
        Rect original{};
        bool resize{};
    } drag_;
    Atom atom(const char *);
    Client *find_client(Window);
    bool is_visible(const Client &) const;
    void set_cardinal(Window, const char *, unsigned long);
    void set_window_property(Window, const char *, Window);
    void set_client_state(Client &, long);
    void manage_window(Window);
    void try_swallow(Window, bool transient);
    void unmanage_window(Window, bool);
    void arrange();
    void set_geometry(Client &, Rect);
    void focus(Window);
    void restack();
    void cycle_focus(int, bool include_hidden = false);
    void set_fullscreen(Client &, bool);
    void toggle_floating(Client &);
    void handle_key_press(const XKeyEvent &);
    void handle_button_press(const XButtonEvent &);
    void handle_event(XEvent &);
    void handle_configure_request(const XConfigureRequestEvent &);
    void handle_motion(const XMotionEvent &);
    void handle_client_message(const XClientMessageEvent &);
    void grab_keys();
    void grab_buttons(const Client &, bool focused);
    void view_tag(unsigned);
    void view_tags(TagMask, bool toggle = false);
    bool on_current_tags(const Client &) const;
    void publish_client_tags(const Client &);
    void assign_tags(Client &, TagMask, bool follow = false, bool toggle = false);
    void select_layout(std::size_t);
    void toggle_layout();
    void assign_tag(Client &, unsigned, bool follow = false);
    Rect workarea() const;
    void hide(Client &);
    void show_all();
    void toggle_scratchpad();
    void promote_to_master();
    void move_in_stack(int);
    void update_client_list();
    void close_window(Client &);
    void send_configure_notify(Client &);
    void spawn(const char *);
    void quit();
    std::string window_title(Window);

  public:
    WindowManager();
    ~WindowManager();
    WindowManager(const WindowManager &) = delete;
    WindowManager &operator=(const WindowManager &) = delete;
    void run();
    [[nodiscard]] bool restart_requested() const noexcept {
        return restart_requested_;
    }
};
} // namespace ppwm
