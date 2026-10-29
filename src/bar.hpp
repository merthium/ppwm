#pragma once
#include "config_types.hpp"
#include <X11/Xft/Xft.h>
#include <X11/Xlib.h>
#include <chrono>
#include <string>
#include <vector>
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
    XftFont *font_{};
    XftColor foreground_{}, background_{}, selection_{};
    XftColor muted_{}, bright_{}, title_background_{}, divider_{};
    enum class TextStyle { Normal, Selected, Title, Hidden };
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
    void draw_text(int x, int width, const std::string &value, TextStyle style = TextStyle::Normal);

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
