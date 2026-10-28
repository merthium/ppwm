#pragma once
#include <X11/Xlib.h>
#include <vector>

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

  private:
    struct Icon {
        Window window;
        bool mapped;
    };
    Display *display_;
    Window root_, bar_, window_{};
    Atom selection_, opcode_, xembed_, xembed_info_;
    int bar_width_, width_{};
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
