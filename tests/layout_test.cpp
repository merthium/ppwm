#include "layout.hpp"
#include <cstdlib>
#include <iostream>
void check(bool ok) {
    if (!ok) {
        std::cerr << "layout invariant failed\n";
        std::exit(1);
    }
}
int main() {
    using namespace ppwm;
    check(tile({0, 24, 1000, 776}, 0, 0.55, 8).empty());
    auto one = tile({0, 24, 1000, 776}, 1, 0.55, 8);
    check(one.size() == 1 && one[0].x == 8 && one[0].y == 32 && one[0].width == 984 &&
          one[0].height == 760);
    for (int n = 1; n < 40; ++n) {
        auto rs = tile({100, 24, 1000, 776}, n, 0.55, 8);
        check(rs.size() == static_cast<unsigned>(n));
        for (auto r : rs)
            check(r.x >= 100 && r.y >= 24 && r.width > 0 && r.height > 0 && r.x + r.width <= 1100 &&
                  r.y + r.height <= 800);
        for (int i = 0; i < n; ++i)
            for (int j = i + 1; j < n; ++j) {
                auto a = rs[i], b = rs[j];
                check(a.x + a.width <= b.x || b.x + b.width <= a.x || a.y + a.height <= b.y ||
                      b.y + b.height <= a.y);
            }
    }
    for (auto r : tile({0, 0, 3, 2}, 12, 0.99, 100))
        check(r.width > 0 && r.height > 0);
    check(monocle({0, 0, 800, 600}, 3, 0.5, 8).size() == 3);
    check(constrain_floating({-20, 900, 1200, 800}, {0, 0, 1024, 744}, 2) == Rect{0, 0, 1020, 740});
    check(constrain_floating({900, 700, 300, 200}, {0, 0, 1024, 744}, 2) ==
          Rect{720, 540, 300, 200});
    check(constrain_floating({10, -30, 100, 100}, {0, 24, 800, 576}, 2) == Rect{10, 24, 100, 100});
    auto masters = tile({0, 0, 1000, 800}, 4, 0.55, 0, 2);
    check(masters[0] == Rect{0, 0, 550, 400} && masters[1] == Rect{0, 400, 550, 400});
    check(masters[2] == Rect{550, 0, 450, 400} && masters[3] == Rect{550, 400, 450, 400});
    auto no_master = tile({0, 0, 1000, 800}, 2, 0.55, 0, 0);
    check(no_master[0] == Rect{0, 0, 1000, 400} && no_master[1] == Rect{0, 400, 1000, 400});
    std::cout << "layout invariants passed\n";
}
