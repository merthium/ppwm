#include "bar.hpp"
#include "bar_modules.hpp"
#include "config.hpp"
#include <algorithm>
#include <array>
#include <ctime>
#include <stdexcept>
namespace ppwm {
Bar::Bar(Display *display, int screen, int width) : display_(display), screen_number_(screen) {
    auto visual = DefaultVisual(display_, screen_number_);
    auto colormap = DefaultColormap(display_, screen_number_);
    font_ = XftFontOpenName(display_, screen_number_, config::font);
    if (!font_)
        throw std::runtime_error("cannot load bar font");
    XftColorAllocName(display_, visual, colormap, config::foreground, &foreground_);
    XftColorAllocName(display_, visual, colormap, config::background, &background_);
    XftColorAllocName(display_, visual, colormap, config::accent, &selection_);
    XSetWindowAttributes attributes{};
    attributes.override_redirect = True;
    attributes.background_pixel = background_.pixel;
    attributes.event_mask = ButtonPressMask;
    window_ = XCreateWindow(display_, RootWindow(display_, screen_number_), 0, 0,
                            static_cast<unsigned>(width), config::bar_height, 0, CopyFromParent,
                            InputOutput, CopyFromParent,
                            CWOverrideRedirect | CWBackPixel | CWEventMask, &attributes);
    XStoreName(display_, window_, "ppwm-bar");
    copy_gc_ = XCreateGC(display_, window_, 0, nullptr);
    resize(width);
    XMapRaised(display_, window_);
}
Bar::~Bar() {
    if (text_draw_)
        XftDrawDestroy(text_draw_);
    if (buffer_)
        XFreePixmap(display_, buffer_);
    XFreeGC(display_, copy_gc_);
    XDestroyWindow(display_, window_);
    XftFontClose(display_, font_);
    auto v = DefaultVisual(display_, screen_number_);
    auto c = DefaultColormap(display_, screen_number_);
    XftColorFree(display_, v, c, &foreground_);
    XftColorFree(display_, v, c, &background_);
    XftColorFree(display_, v, c, &selection_);
}
void Bar::resize(int width) {
    width_ = std::max(1, width);
    if (text_draw_)
        XftDrawDestroy(text_draw_);
    if (buffer_)
        XFreePixmap(display_, buffer_);
    XResizeWindow(display_, window_, static_cast<unsigned>(width_), config::bar_height);
    buffer_ = XCreatePixmap(display_, window_, static_cast<unsigned>(width_), config::bar_height,
                            static_cast<unsigned>(DefaultDepth(display_, screen_number_)));
    text_draw_ = XftDrawCreate(display_, buffer_, DefaultVisual(display_, screen_number_),
                               DefaultColormap(display_, screen_number_));
}
void Bar::draw_text(int x, int width, const std::string &value, bool active) {
    if (width <= 0)
        return;
    XRectangle clip{static_cast<short>(x), 0, static_cast<unsigned short>(std::min(width, 65535)),
                    config::bar_height};
    XftDrawSetClipRectangles(text_draw_, 0, 0, &clip, 1);
    if (active)
        XftDrawRect(text_draw_, &selection_, x, 0, static_cast<unsigned>(width),
                    config::bar_height);
    XftDrawStringUtf8(text_draw_, &foreground_, font_, x + 7,
                      (config::bar_height - font_->height) / 2 + font_->ascent,
                      reinterpret_cast<const FcChar8 *>(value.data()),
                      static_cast<int>(value.size()));
    XftDrawSetClip(text_draw_, nullptr);
}
BarHit Bar::hit_test(int x) const {
    for (const auto &region : regions_)
        if (x >= region.left && x < region.right)
            return region.hit;
    return {};
}
void Bar::render(const BarState &state, int reserved_width) {
    const int content_width = std::max(0, width_ - reserved_width);
    XftDrawRect(text_draw_, &background_, 0, 0, static_cast<unsigned>(width_), config::bar_height);
    for (unsigned i = 0; i < 9; ++i) {
        int x = static_cast<int>(i) * config::tag_width;
        draw_text(x, config::tag_width, std::to_string(i + 1),
                  (state.active_tags & (1u << i)) != 0);
        if (state.occupied & (1u << i))
            XftDrawRect(text_draw_, &foreground_, x + 7, config::bar_height - 3, 12, 2);
    }
    int left = 9 * config::tag_width;
    draw_text(left, 50, state.floating ? "[F]" : state.layout);
    left += 50;
    regions_.clear();
    std::array<std::string, bar_modules::modules.size()> parts;
    std::array<int, bar_modules::modules.size()> widths{};
    int status_width = 0;
    for (std::size_t i = 0; i < parts.size(); ++i) {
        parts[i] = bar_modules::modules[i].read(state);
        if (parts[i].empty())
            continue;
        XGlyphInfo extent{};
        XftTextExtentsUtf8(display_, font_, reinterpret_cast<const FcChar8 *>(parts[i].data()),
                           static_cast<int>(parts[i].size()), &extent);
        widths[i] = static_cast<int>(extent.xOff) + 16;
        status_width += widths[i];
    }
    status_width = std::min(std::max(0, content_width - left), status_width);
    const int title_width = std::max(0, content_width - left - status_width);
    const int count = static_cast<int>(state.clients.size());
    for (int i = 0; i < count; ++i) {
        const auto &client = state.clients[static_cast<std::size_t>(i)];
        const int start = left + title_width * i / count;
        const int end = left + title_width * (i + 1) / count;
        draw_text(start, end - start, client.hidden ? "[" + client.title + "]" : client.title,
                  client.selected);
        regions_.push_back({start, end, {client.window, -1}});
    }
    int position = content_width - status_width;
    for (std::size_t i = 0; i < parts.size(); ++i) {
        const int width = std::min(widths[i], content_width - position);
        draw_text(position, width, parts[i]);
        regions_.push_back({position, position + width, {None, static_cast<int>(i)}});
        position += width;
    }
    XCopyArea(display_, buffer_, window_, copy_gc_, 0, 0, static_cast<unsigned>(width_),
              config::bar_height, 0, 0);
    XFlush(display_);
}
} // namespace ppwm
