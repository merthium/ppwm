#include "bar.hpp"
#include "config.hpp"
#include <X11/Xatom.h>
#include <algorithm>
namespace ppwm {
int Bar::text_width(const std::string &text) const {
    XGlyphInfo extent{};
    XftTextExtentsUtf8(display_, font_, reinterpret_cast<const FcChar8 *>(text.data()),
                       static_cast<int>(text.size()), &extent);
    return extent.xOff;
}
void Bar::read_external_status() {
    external_status_ = false;
    external_parts_.clear();
    if (!config::use_slstatus)
        return;
    Atom type{};
    int format{};
    unsigned long count{}, remaining{};
    unsigned char *raw = nullptr;
    if (XGetWindowProperty(display_, RootWindow(display_, screen_number_), XA_WM_NAME, 0, 1024,
                           False, AnyPropertyType, &type, &format, &count, &remaining,
                           &raw) == Success &&
        raw && format == 8 && count) {
        external_parts_ = parse_status(std::string_view(reinterpret_cast<char *>(raw), count));
    }
    if (raw)
        XFree(raw);
    int cursor = 0;
    bool visible = false;
    for (const auto &part : external_parts_) {
        if (part.kind == StatusKind::Text) {
            cursor += text_width(part.text);
            visible |= !part.text.empty();
        }
        if (part.kind == StatusKind::Offset)
            cursor += part.x;
        if (part.kind == StatusKind::Rectangle)
            visible = true;
        cursor = std::clamp(cursor, 0, 16384);
    }
    external_status_ = visible;
    external_width_ = cursor + 2 * config::text_padding;
}
void Bar::draw_external_status(int x, int width) {
    if (width <= 0)
        return;
    XRectangle clip{static_cast<short>(x), 0, static_cast<unsigned short>(std::min(width, 65535)),
                    config::bar_height};
    XftDrawSetClipRectangles(text_draw_, 0, 0, &clip, 1);
    const XftColor *foreground = &foreground_, *background = &background_;
    auto color = [&](const std::string &name, const XftColor *fallback) -> const XftColor * {
        if (auto found = status_colors_.find(name); found != status_colors_.end())
            return &found->second;
        if (status_colors_.size() >= 64)
            return fallback;
        XftColor allocated{};
        if (!XftColorAllocName(display_, DefaultVisual(display_, screen_number_),
                               DefaultColormap(display_, screen_number_), name.c_str(), &allocated))
            return fallback;
        return &status_colors_.emplace(name, allocated).first->second;
    };
    int cursor = 0;
    for (const auto &part : external_parts_) {
        switch (part.kind) {
        case StatusKind::Foreground:
            foreground = color(part.text, &foreground_);
            break;
        case StatusKind::Background:
            background = color(part.text, &background_);
            break;
        case StatusKind::Reset:
            foreground = &foreground_;
            background = &background_;
            break;
        case StatusKind::Offset:
            cursor = std::clamp(cursor + part.x, 0, 16384);
            break;
        case StatusKind::Rectangle:
            XftDrawRect(text_draw_, foreground, x + config::text_padding + cursor + part.x, part.y,
                        static_cast<unsigned>(part.width), static_cast<unsigned>(part.height));
            break;
        case StatusKind::Text: {
            const int advance = text_width(part.text);
            XftDrawRect(text_draw_, background, x + config::text_padding + cursor, 0,
                        static_cast<unsigned>(advance), config::bar_height);
            XftDrawStringUtf8(text_draw_, foreground, font_, x + config::text_padding + cursor,
                              (config::bar_height - font_->height) / 2 + font_->ascent,
                              reinterpret_cast<const FcChar8 *>(part.text.data()),
                              static_cast<int>(part.text.size()));
            cursor = std::clamp(cursor + advance, 0, 16384);
            break;
        }
        }
    }
    XftDrawSetClip(text_draw_, nullptr);
}
} // namespace ppwm
