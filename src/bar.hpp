#pragma once
#include <X11/Xft/Xft.h>
#include <X11/Xlib.h>
#include <string>
#include <vector>
namespace ppwm {
struct BarClient {
    Window window{};
    std::string title;
    bool selected{}, hidden{};
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
};
struct BarModule {
    std::string (*read)(const BarState &);
    const char *on_click{};
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
