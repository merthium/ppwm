#include <X11/Xatom.h>
#include <X11/Xlib.h>
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
        const Window root = DefaultRootWindow(d);
        auto attributes = [&](Window w) {
            XWindowAttributes a{};
            XGetWindowAttributes(d, w, &a);
            return a;
        };
        auto wait = [&](auto predicate, const char *message) {
            auto end = std::chrono::steady_clock::now() + std::chrono::seconds(3);
            while (!predicate()) {
                if (std::chrono::steady_clock::now() > end)
                    throw std::runtime_error(message);
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
            }
        };
        auto send = [&](Window w, const char *name, long value) {
            XEvent e{};
            e.xclient.type = ClientMessage;
            e.xclient.window = w;
            e.xclient.message_type = XInternAtom(d, name, False);
            e.xclient.format = 32;
            e.xclient.data.l[0] = value;
            XSendEvent(d, root, False, SubstructureRedirectMask | SubstructureNotifyMask, &e);
            XFlush(d);
        };
        auto key = [&](const char *command) {
            if (std::system(command) != 0)
                throw std::runtime_error("test key injection failed");
        };
        send(root, "_NET_CURRENT_DESKTOP", 0);
        Window a = XCreateSimpleWindow(d, root, 20, 20, 200, 100, 0, 0, 0);
        XMapWindow(d, a);
        XFlush(d);
        wait(
            [&] {
                const auto x = attributes(a);
                return x.map_state == IsViewable && x.border_width == 0 && x.width == 1024 &&
                       x.height == 744;
            },
            "single tiled client fills workarea without border");
        Window b = XCreateSimpleWindow(d, root, 40, 40, 200, 100, 0, 0, 0);
        XMapWindow(d, b);
        XFlush(d);
        wait([&] { return attributes(a).border_width == 2 && attributes(b).border_width == 2; },
             "second visible client restores borders");
        send(b, "_NET_WM_DESKTOP", 1);
        wait(
            [&] {
                return attributes(b).map_state == IsUnmapped && attributes(a).border_width == 0 &&
                       attributes(a).width == 1024;
            },
            "tag-hidden windows do not force a border");
        send(b, "_NET_WM_DESKTOP", 0);
        wait(
            [&] {
                return attributes(b).map_state == IsViewable && attributes(a).border_width == 2;
            },
            "returning client restores borders");
        send(b, "_NET_ACTIVE_WINDOW", 1);
        key("xdotool key --clearmodifiers alt+h");
        wait(
            [&] {
                return attributes(b).map_state == IsUnmapped && attributes(a).border_width == 0;
            },
            "hidden windows do not force a border");
        send(b, "_NET_ACTIVE_WINDOW", 1);
        wait(
            [&] {
                return attributes(b).map_state == IsViewable && attributes(a).border_width == 2;
            },
            "unhiding restores borders");
        XDestroyWindow(d, b);
        XFlush(d);
        wait([&] { return attributes(a).border_width == 0; },
             "closing second client removes border");
        send(a, "_NET_ACTIVE_WINDOW", 1);
        key("xdotool key --clearmodifiers alt+F11");
        wait(
            [&] {
                const auto x = attributes(a);
                return x.border_width == 0 && x.height == 768;
            },
            "single fullscreen client has no border");
        key("xdotool key --clearmodifiers alt+F11");
        wait(
            [&] {
                const auto x = attributes(a);
                return x.border_width == 0 && x.height == 744;
            },
            "fullscreen exit respects smartborder");
        key("xdotool key --clearmodifiers alt+shift+space");
        XMoveResizeWindow(d, a, 100, 120, 240, 180);
        XFlush(d);
        wait(
            [&] {
                const auto x = attributes(a);
                return x.border_width == 0 && x.width == 240 && x.height == 180;
            },
            "single floating client remains borderless");
        XMoveWindow(d, a, 1024, 768);
        XFlush(d);
        wait(
            [&] {
                const auto x = attributes(a);
                return x.x == 784 && x.y == 564;
            },
            "floating edge constraints use the actual zero-width border");
        XDestroyWindow(d, a);
        XCloseDisplay(d);
        std::cout << "Smartborder transitions passed\n";
    } catch (const std::exception &e) {
        std::cerr << "FAIL: " << e.what() << '\n';
        XCloseDisplay(d);
        return 1;
    }
}
