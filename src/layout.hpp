#pragma once
#include <algorithm>
#include <array>
#include <vector>

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
inline constexpr std::array layouts{Layout{"t", tile}, Layout{"f", nullptr}, Layout{"m", monocle}};
} // namespace ppwm
