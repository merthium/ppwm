#include "config.hpp"
#include <X11/Xatom.h>
#include <X11/Xutil.h>
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <thread>

namespace {
void require(bool condition, const char *message) {
    if (!condition)
        throw std::runtime_error(message);
}
template <class Predicate> void wait_until(Predicate condition, const char *message) {
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    while (!condition()) {
        require(std::chrono::steady_clock::now() < deadline, message);
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
}
XWindowAttributes attributes(Display *display, Window window) {
    XWindowAttributes result{};
    require(XGetWindowAttributes(display, window, &result), "read window attributes");
    return result;
}
unsigned long property(Display *display, Window window, const char *name) {
    Atom type = None;
    int format = 0;
    unsigned long count = 0, remaining = 0;
    unsigned char *data = nullptr;
    XGetWindowProperty(display, window, XInternAtom(display, name, False), 0, 1, False,
                       AnyPropertyType, &type, &format, &count, &remaining, &data);
    unsigned long value = ~0UL;
    if (data && count == 1 && format == 32)
        value = *reinterpret_cast<unsigned long *>(data);
    if (data)
        XFree(data);
    return value;
}
void send(Display *display, Window window, const char *name, long value) {
    XEvent event{};
    event.xclient.type = ClientMessage;
    event.xclient.window = window;
    event.xclient.message_type = XInternAtom(display, name, False);
    event.xclient.format = 32;
    event.xclient.data.l[0] = value;
    XSendEvent(display, DefaultRootWindow(display), False,
               SubstructureRedirectMask | SubstructureNotifyMask, &event);
    XFlush(display);
}
void click(Display *display, Window bar, int x, unsigned button) {
    XEvent event{};
    event.xbutton.type = ButtonPress;
    event.xbutton.display = display;
    event.xbutton.window = bar;
    event.xbutton.root = DefaultRootWindow(display);
    event.xbutton.x = x;
    event.xbutton.y = 5;
    event.xbutton.button = button;
    event.xbutton.same_screen = True;
    XSendEvent(display, bar, False, ButtonPressMask, &event);
    XFlush(display);
}
Window find_bar(Display *display) {
    Window root = None, parent = None, *children = nullptr, result = None;
    unsigned count = 0;
    XQueryTree(display, DefaultRootWindow(display), &root, &parent, &children, &count);
    for (unsigned i = 0; i < count; ++i) {
        char *name = nullptr;
        if (XFetchName(display, children[i], &name) && name) {
            if (std::strcmp(name, "ppwm-bar") == 0)
                result = children[i];
            XFree(name);
        }
    }
    if (children)
        XFree(children);
    return result;
}
unsigned long pixel(Display *display, Window window, int x) {
    XImage *image = XGetImage(display, window, x, 0, 1, 1, AllPlanes, ZPixmap);
    require(image, "read bar pixel");
    const auto value = XGetPixel(image, 0, 0);
    XDestroyImage(image);
    return value;
}
void run(Display *display) {
    const Window root = DefaultRootWindow(display);
    const Window bar = find_bar(display);
    require(bar != None, "find integrated bar");
    send(display, root, "_NET_CURRENT_DESKTOP", 0);
    wait_until([&] { return property(display, root, "_NET_CURRENT_DESKTOP") == 0; },
               "view first tag");
    auto make_window = [&](const char *title) {
        Window window = XCreateSimpleWindow(display, root, 30, 40, 200, 100, 0, 0, 0);
        XStoreName(display, window, title);
        XMapWindow(display, window);
        XFlush(display);
        wait_until([&] { return property(display, window, "WM_STATE") == NormalState; },
                   "manage test window");
        return window;
    };
    const Window first = make_window("bar-first");
    const Window second = make_window("bar-second");
    send(display, second, "_NET_ACTIVE_WINDOW", 1);
    wait_until([&] { return property(display, root, "_NET_ACTIVE_WINDOW") == second; },
               "select second window");
    constexpr int left = 9 * ppwm::config::tag_width + 50;
    XColor selected_color{}, exact{};
    require(XAllocNamedColor(display, DefaultColormap(display, DefaultScreen(display)),
                             ppwm::config::accent, &selected_color, &exact),
            "selection color");
    const auto selected_pixel = selected_color.pixel;
    wait_until([&] { return pixel(display, bar, left + 5) == selected_pixel; },
               "selected first tab painted");
    int boundary = left + 6;
    while (boundary < attributes(display, bar).width &&
           pixel(display, bar, boundary) == selected_pixel)
        ++boundary;
    require(boundary > left + 20 && boundary < attributes(display, bar).width,
            "find title tab boundary");
    const int selected_tab = left + 10;
    const int other_tab = boundary + 10;
    // Erase one selected pixel without Expose: only a later timer redraw restores it.
    XClearArea(display, bar, left + 5, 0, 1, 1, False);
    XSync(display, False);
    wait_until([&] { return pixel(display, bar, left + 5) == selected_pixel; },
               "periodic bar render restores selected tab");
    click(display, bar, selected_tab, Button1);
    wait_until(
        [&] {
            return attributes(display, second).map_state == IsUnmapped &&
                   property(display, second, "WM_STATE") == IconicState;
        },
        "selected tab click hides window after periodic render");
    click(display, bar, selected_tab, Button1);
    wait_until(
        [&] {
            return attributes(display, second).map_state == IsViewable &&
                   property(display, root, "_NET_ACTIVE_WINDOW") == second;
        },
        "hidden tab click restores window");
    click(display, bar, other_tab, Button1);
    wait_until(
        [&] {
            return property(display, root, "_NET_ACTIVE_WINDOW") == first &&
                   attributes(display, second).map_state == IsViewable;
        },
        "other tab click focuses without hiding selected window");

    send(display, first, "_NET_WM_DESKTOP", 1);
    wait_until([&] { return attributes(display, first).map_state == IsUnmapped; },
               "move first window to second tag");
    click(display, bar, ppwm::config::tag_width + 5, Button3);
    wait_until(
        [&] {
            return attributes(display, first).map_state == IsViewable &&
                   attributes(display, second).map_state == IsViewable;
        },
        "right click adds second tag to active view");
    click(display, bar, 5, Button3);
    wait_until(
        [&] {
            return attributes(display, second).map_state == IsUnmapped &&
                   property(display, root, "_NET_CURRENT_DESKTOP") == 1;
        },
        "right click removes first tag from combined view");
    require(std::system("xdotool key --clearmodifiers alt+b") == 0, "toggle second-tag bar");
    wait_until([&] { return attributes(display, bar).map_state == IsUnmapped; },
               "second tag bar hidden");
    send(display, root, "_NET_CURRENT_DESKTOP", 0);
    wait_until(
        [&] {
            return attributes(display, bar).map_state == IsViewable &&
                   attributes(display, second).map_state == IsViewable;
        },
        "first tag retains visible bar setting");
    send(display, root, "_NET_CURRENT_DESKTOP", 1);
    wait_until(
        [&] {
            return attributes(display, bar).map_state == IsUnmapped &&
                   attributes(display, first).map_state == IsViewable;
        },
        "second tag retains hidden bar setting");
    require(std::system("xdotool key --clearmodifiers alt+b") == 0, "restore second-tag bar");
    wait_until([&] { return attributes(display, bar).map_state == IsViewable; }, "bar restored");
    XDestroyWindow(display, first);
    XDestroyWindow(display, second);
    send(display, root, "_NET_CURRENT_DESKTOP", 0);
    XSync(display, False);
}
} // namespace
int main() {
    try {
        const char *expected = std::getenv("PPWM_TEST_DISPLAY");
        const char *actual = std::getenv("DISPLAY");
        require(expected && actual && std::strcmp(expected, actual) == 0,
                "use scripts/nested-test.py; refusing a non-test display");
        std::unique_ptr<Display, decltype(&XCloseDisplay)> display(XOpenDisplay(nullptr),
                                                                   XCloseDisplay);
        require(static_cast<bool>(display), "display connection");
        run(display.get());
        std::cout << "Bar tabs, multitag and per-tag integration passed\n";
    } catch (const std::exception &error) {
        std::cerr << "FAIL: " << error.what() << '\n';
        return 1;
    }
}
