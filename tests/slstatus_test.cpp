#include <X11/Xatom.h>
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <stdexcept>
#include <thread>
int main() {
    const char *expected = std::getenv("PPWM_TEST_DISPLAY"), *actual = std::getenv("DISPLAY");
    if (!expected || !actual || std::strcmp(expected, actual))
        return 1;
    Display *d = XOpenDisplay(nullptr);
    if (!d)
        return 1;
    try {
        Window root = DefaultRootWindow(d), rr{}, parent{}, *children = nullptr, bar = None;
        unsigned count{};
        XQueryTree(d, root, &rr, &parent, &children, &count);
        for (unsigned i = 0; i < count; ++i) {
            char *name = nullptr;
            if (XFetchName(d, children[i], &name) && name) {
                if (!std::strcmp(name, "ppwm-bar"))
                    bar = children[i];
                XFree(name);
            }
        }
        if (children)
            XFree(children);
        if (!bar)
            throw std::runtime_error("missing bar");
        const auto hash = [&] {
            XWindowAttributes a{};
            XGetWindowAttributes(d, bar, &a);
            auto *image = XGetImage(d, bar, a.width - 100, 0, 100, 24, AllPlanes, ZPixmap);
            if (!image)
                throw std::runtime_error("bar capture");
            unsigned long result = 5381;
            for (int y = 0; y < 24; ++y)
                for (int x = 0; x < 100; ++x)
                    result = result * 33 + XGetPixel(image, x, y);
            XDestroyImage(image);
            return result;
        };
        const auto wait = [&](auto predicate, const char *message) {
            auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
            while (!predicate()) {
                if (std::chrono::steady_clock::now() > deadline)
                    throw std::runtime_error(message);
                std::this_thread::sleep_for(std::chrono::milliseconds(20));
            }
        };
        XStoreName(d, root, "^c#ff0000^^r0,0,20,24f20^");
        XFlush(d);
        XColor red{}, exact{};
        XAllocNamedColor(d, DefaultColormap(d, DefaultScreen(d)), "#ff0000", &red, &exact);
        wait(
            [&] {
                XWindowAttributes a{};
                XGetWindowAttributes(d, bar, &a);
                auto *image = XGetImage(d, bar, a.width - 20, 4, 1, 1, AllPlanes, ZPixmap);
                bool match = image && XGetPixel(image, 0, 0) == red.pixel;
                if (image)
                    XDestroyImage(image);
                return match;
            },
            "slstatus status2d color/rectangle not rendered");
        auto previous = hash();
        XStoreName(d, root, "slstatus plain UTF-8: saat");
        XFlush(d);
        wait([&] { return hash() != previous; }, "plain root name not refreshed");
        previous = hash();
        XDeleteProperty(d, root, XA_WM_NAME);
        XFlush(d);
        wait([&] { return hash() != previous; }, "native status fallback not restored");
        XCloseDisplay(d);
        std::cout << "slstatus root-name, status2d and fallback passed\n";
    } catch (const std::exception &e) {
        std::cerr << "FAIL: " << e.what() << '\n';
        XCloseDisplay(d);
        return 1;
    }
}
