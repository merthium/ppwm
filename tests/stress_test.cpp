#include <X11/Xatom.h>
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <thread>
#include <vector>

using Clock = std::chrono::steady_clock;
void require(bool condition, const char *message) {
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        std::exit(1);
    }
}

int main() {
    const char *test_display = std::getenv("PPWM_TEST_DISPLAY");
    const char *display_name = std::getenv("DISPLAY");
    require(test_display && display_name && std::strcmp(test_display, display_name) == 0,
            "use scripts/nested-test.py");
    Display *display = XOpenDisplay(nullptr);
    require(display, "open nested display");
    Window root = DefaultRootWindow(display);
    const Atom client_list = XInternAtom(display, "_NET_CLIENT_LIST", False);
    const Atom current_tag = XInternAtom(display, "_NET_CURRENT_DESKTOP", False);
    auto property = [&](Atom property_name) {
        Atom type;
        int format;
        unsigned long count, remaining;
        unsigned char *bytes = nullptr;
        XGetWindowProperty(display, root, property_name, 0, 4096, False, AnyPropertyType, &type,
                           &format, &count, &remaining, &bytes);
        std::vector<unsigned long> values;
        if (bytes && format == 32) {
            auto *data = reinterpret_cast<unsigned long *>(bytes);
            values.assign(data, data + count);
        }
        if (bytes)
            XFree(bytes);
        return values;
    };
    auto wait_until = [&](auto predicate, const char *message) {
        auto deadline = Clock::now() + std::chrono::seconds(10);
        while (!predicate()) {
            require(Clock::now() < deadline, message);
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
        }
    };
    auto view_tag = [&](long tag) {
        XEvent event{};
        event.xclient.type = ClientMessage;
        event.xclient.window = root;
        event.xclient.message_type = current_tag;
        event.xclient.format = 32;
        event.xclient.data.l[0] = tag;
        XSendEvent(display, root, False, SubstructureRedirectMask | SubstructureNotifyMask, &event);
        XFlush(display);
        wait_until(
            [&] {
                auto values = property(current_tag);
                return !values.empty() && values[0] == static_cast<unsigned long>(tag);
            },
            "tag barrier timeout");
    };
    view_tag(0);
    auto started = Clock::now();
    std::vector<Window> windows;
    constexpr int window_count = 64;
    for (int index = 0; index < window_count; ++index) {
        Window window = XCreateSimpleWindow(display, root, 40, 40, 280, 160, 0, 0, 0);
        char instance[] = "stress", class_name[] = "popup";
        XClassHint hint{instance, class_name};
        XSetClassHint(display, window, &hint);
        XStoreName(display, window, "ppwm stress");
        XMapWindow(display, window);
        windows.push_back(window);
    }
    XFlush(display);
    wait_until([&] { return property(client_list).size() == window_count; },
               "manage burst timeout");
    auto mapped = Clock::now();
    constexpr int requests = 10000;
    for (int index = 0; index < requests; ++index)
        XMoveResizeWindow(display, windows[0], index % 500, index % 300, 280, 160);
    // The WM processes this message after the preceding configure requests.
    view_tag(1);
    auto configured = Clock::now();
    XWindowAttributes attributes{};
    XGetWindowAttributes(display, windows[0], &attributes);
    require(attributes.x == 499 && attributes.y == 99 && attributes.width == 280 &&
                attributes.height == 160,
            "configure burst preserves final geometry");
    view_tag(0);
    for (Window window : windows)
        XDestroyWindow(display, window);
    XFlush(display);
    wait_until([&] { return property(client_list).empty(); }, "destroy burst timeout");
    auto finished = Clock::now();
    auto milliseconds = [](auto duration) {
        return std::chrono::duration<double, std::milli>(duration).count();
    };
    std::cout << window_count << " windows: map " << milliseconds(mapped - started) << " ms; "
              << requests
              << " configure requests + tag barrier: " << milliseconds(configured - mapped)
              << " ms; restore/destroy " << milliseconds(finished - configured) << " ms\n";
    XCloseDisplay(display);
}
