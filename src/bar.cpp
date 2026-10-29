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
    XftColorAllocName(display_, visual, colormap, config::bar_muted, &muted_);
    XftColorAllocName(display_, visual, colormap, config::bar_bright, &bright_);
    XftColorAllocName(display_, visual, colormap, config::bar_title_background, &title_background_);
    XftColorAllocName(display_, visual, colormap, config::bar_divider, &divider_);
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
    XftColorFree(display_, v, c, &muted_);
    XftColorFree(display_, v, c, &bright_);
    XftColorFree(display_, v, c, &title_background_);
    XftColorFree(display_, v, c, &divider_);
}
void Bar::resize(int width) {
    width_ = std::max(1, width);
    painted_ = false;
    if (text_draw_)
        XftDrawDestroy(text_draw_);
    if (buffer_)
        XFreePixmap(display_, buffer_);
    XResizeWindow(display_, window_, static_cast<unsigned>(width_), config::bar_height);
    buffer_ = XCreatePixmap(display_, window_, static_cast<unsigned>(width_), config::bar_height,
                            static_cast<unsigned>(DefaultDepth(display_, screen_number_)));
    text_draw_ = XftDrawCreate(display_, buffer_, DefaultVisual(display_, screen_number_),
                               DefaultColormap(display_, screen_number_));
    XftDrawRect(text_draw_, &background_, 0, 0, static_cast<unsigned>(width_), config::bar_height);
    // The X server can repaint exposed/remapped areas from the most recent frame.
    XSetWindowBackgroundPixmap(display_, window_, buffer_);
}
void Bar::draw_text(int x, int width, const std::string &value, TextStyle style) {
    if (width <= 0)
        return;
    XRectangle clip{static_cast<short>(x), 0, static_cast<unsigned short>(std::min(width, 65535)),
                    config::bar_height};
    XftDrawSetClipRectangles(text_draw_, 0, 0, &clip, 1);
    const auto *background = style == TextStyle::Selected ? &selection_
                             : style == TextStyle::Title  ? &title_background_
                                                          : &background_;
    const auto *foreground = style == TextStyle::Selected ? &bright_
                             : style == TextStyle::Hidden ? &muted_
                                                          : &foreground_;
    XftDrawRect(text_draw_, background, x, 0, static_cast<unsigned>(width), config::bar_height);
    XftDrawStringUtf8(text_draw_, foreground, font_, x + config::text_padding,
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
    const auto now = std::chrono::steady_clock::now();
    const bool refresh_status = now >= next_status_;
    if (refresh_status) {
        status_parts_.resize(config::status_modules.size());
        status_widths_.resize(config::status_modules.size());
        for (std::size_t i = 0; i < status_parts_.size(); ++i) {
            status_parts_[i] = config::status_modules[i].read(state);
            XGlyphInfo extent{};
            XftTextExtentsUtf8(display_, font_,
                               reinterpret_cast<const FcChar8 *>(status_parts_[i].data()),
                               static_cast<int>(status_parts_[i].size()), &extent);
            status_widths_[i] = status_parts_[i].empty()
                                    ? 0
                                    : static_cast<int>(extent.xOff) + 2 * config::text_padding + 2;
        }
        next_status_ = now + config::status_interval;
    }
    // The UI is sampled by a timer; unchanged ticks make no X11 drawing calls.
    if (painted_ && !refresh_status && reserved_width == last_reserved_ && state == last_state_)
        return;
    last_state_ = state;
    last_reserved_ = reserved_width;
    painted_ = true;
    const int content_width = std::max(0, width_ - reserved_width);
    XftDrawRect(text_draw_, &background_, 0, 0, static_cast<unsigned>(width_), config::bar_height);
    for (unsigned i = 0; i < 9; ++i) {
        int x = static_cast<int>(i) * config::tag_width;
        draw_text(x, config::tag_width, std::string(config::tag_labels[i]),
                  (state.active_tags & (1u << i)) ? TextStyle::Selected
                  : (state.occupied & (1u << i))  ? TextStyle::Normal
                                                  : TextStyle::Hidden);
        if (state.occupied & (1u << i))
            XftDrawRect(text_draw_, &foreground_, x + 7, config::bar_height - 3, 12, 2);
    }
    int left = 9 * config::tag_width;
    draw_text(left, config::layout_width, state.floating ? "[F]" : state.layout);
    left += config::layout_width;
    regions_.clear();
    const auto &parts = status_parts_;
    const auto &widths = status_widths_;
    int status_width = 0;
    for (int width : widths)
        status_width += width;
    status_width = std::min(std::max(0, content_width - left), status_width);
    const int title_width = std::max(0, content_width - left - status_width);
    const int count = static_cast<int>(state.clients.size());
    for (int i = 0; i < count; ++i) {
        const auto &client = state.clients[static_cast<std::size_t>(i)];
        const int start = left + title_width * i / count;
        const int end = left + title_width * (i + 1) / count;
        draw_text(start, end - start, client.hidden ? "[" + client.title + "]" : client.title,
                  client.selected ? TextStyle::Selected
                  : client.hidden ? TextStyle::Hidden
                                  : TextStyle::Title);
        if (end > start) {
            XftDrawRect(text_draw_, &divider_, end - 1, 5, 1, config::bar_height - 10);
            if (client.selected)
                XftDrawRect(text_draw_, &bright_, start + 1, config::bar_height - 2,
                            static_cast<unsigned>(std::max(0, end - start - 2)), 2);
        }
        regions_.push_back({start, end, {client.window, -1}});
    }
    int position = content_width - status_width;
    for (std::size_t i = 0; i < parts.size(); ++i) {
        const int width = std::min(widths[i], content_width - position);
        draw_text(position, width, parts[i]);
        if (width > 0)
            XftDrawRect(text_draw_, &divider_, position, 6, 1, config::bar_height - 12);
        regions_.push_back({position, position + width, {None, static_cast<int>(i)}});
        position += width;
    }
    XCopyArea(display_, buffer_, window_, copy_gc_, 0, 0, static_cast<unsigned>(width_),
              config::bar_height, 0, 0);
    XFlush(display_);
}
} // namespace ppwm
