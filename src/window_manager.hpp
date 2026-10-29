#pragma once
#include "bar.hpp"
#include "layout.hpp"
#include "process.hpp"
#include "system_tray.hpp"
#include "workspace.hpp"
#include "x11_connection.hpp"
#include <X11/Xutil.h>
#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>
namespace ppwm {
struct Client {
    Window window{};
    std::string title;
    TagMask tags{1};
    std::uint64_t focus_order{};
    bool terminal{};
    Window swallowed_by{}, terminal_window{};
    bool floating{}, fullscreen{}, mapped{}, input{true}, hidden{}, scratchpad{};
    int ignore_unmap{}, original_border{};
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
    bool restart_requested_{};
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
