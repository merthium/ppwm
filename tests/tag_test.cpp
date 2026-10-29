#include <X11/Xatom.h>
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <thread>

namespace {
void settle() {
    std::this_thread::sleep_for(std::chrono::milliseconds(180));
}
} // namespace
int main() {
    const char *expected = std::getenv("PPWM_TEST_DISPLAY");
    const char *display = std::getenv("DISPLAY");
    if (!expected || !display || std::strcmp(expected, display) != 0) {
        std::cerr << "Use the isolated nested test harness\n";
        return 2;
    }
    Display *d = XOpenDisplay(nullptr);
    if (!d)
        return 2;
    const Window root = DefaultRootWindow(d);
    int failures = 0;
    auto check = [&](bool passed, const char *description) {
        std::cout << (passed ? "PASS: " : "FAIL: ") << description << '\n';
        failures += !passed;
    };
    auto value = [&](Window window, const char *name) {
        Atom type{};
        int format{};
        unsigned long count{}, remaining{};
        unsigned char *data = nullptr;
        XGetWindowProperty(d, window, XInternAtom(d, name, False), 0, 1, False, AnyPropertyType,
                           &type, &format, &count, &remaining, &data);
        const unsigned long result =
            data && count && format == 32 ? *reinterpret_cast<unsigned long *>(data) : 0;
        if (data)
            XFree(data);
        return result & 0xffffffffUL; // Xlib long storage may sign-extend a 32-bit property.
    };
    auto send = [&](Window window, const char *name, long argument) {
        XEvent event{};
        event.xclient.type = ClientMessage;
        event.xclient.window = window;
        event.xclient.message_type = XInternAtom(d, name, False);
        event.xclient.format = 32;
        event.xclient.data.l[0] = argument;
        XSendEvent(d, root, False, SubstructureRedirectMask | SubstructureNotifyMask, &event);
        XSync(d, False);
        settle();
    };
    auto key = [&](const char *binding) {
        std::string command = "xdotool key --clearmodifiers ";
        command += binding;
        if (std::system(command.c_str()) != 0)
            std::exit(2);
        settle();
    };
    auto mapped = [&](Window window) {
        XWindowAttributes attributes{};
        XGetWindowAttributes(d, window, &attributes);
        return attributes.map_state == IsViewable;
    };
    if (!value(root, "_NET_SUPPORTING_WM_CHECK"))
        return 2;
    if (std::system("xdotool mousemove --sync 1000 760") != 0)
        return 2;
    send(root, "_NET_CURRENT_DESKTOP", 0);
    const Window a = XCreateSimpleWindow(d, root, 50, 50, 200, 150, 0, 0, 0);
    const Window b = XCreateSimpleWindow(d, root, 80, 80, 200, 150, 0, 0, 0);
    XStoreName(d, a, "tag-regression-a");
    XStoreName(d, b, "tag-regression-b");
    XMapWindow(d, a);
    XMapWindow(d, b);
    XSync(d, False);
    settle();

    send(b, "_NET_WM_DESKTOP", 1);
    key("alt+ctrl+2");
    check(mapped(a) && mapped(b), "combined view contains both tags");
    send(a, "_NET_ACTIVE_WINDOW", 1);
    check(mapped(a) && mapped(b), "activating a visible client preserves combined view");

    send(root, "_NET_CURRENT_DESKTOP", 0);
    send(a, "_NET_WM_DESKTOP", 0xffffffffUL);
    check(value(a, "_NET_WM_DESKTOP") == 0xffffffffUL, "EWMH supports all-tags assignment");
    send(root, "_NET_CURRENT_DESKTOP", 2);
    check(mapped(a), "all-tags client remains visible on another tag");
    send(a, "_NET_WM_DESKTOP", 0);
    send(b, "_NET_WM_DESKTOP", 0);
    send(root, "_NET_CURRENT_DESKTOP", 0);

    send(a, "_NET_ACTIVE_WINDOW", 1);
    key("alt+h");
    key("alt+shift+j");
    check(mapped(a) && value(root, "_NET_ACTIVE_WINDOW") == a,
          "hidden focus cycle previews the hidden client");
    send(a, "_NET_ACTIVE_WINDOW", 1);
    send(b, "_NET_ACTIVE_WINDOW", 1);
    check(mapped(a) && value(a, "WM_STATE") == NormalState,
          "explicit activation makes a preview permanent");

    XDestroyWindow(d, a);
    XDestroyWindow(d, b);
    XSync(d, False);
    XCloseDisplay(d);
    return failures ? 1 : 0;
}
