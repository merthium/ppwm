#include <X11/Xatom.h>
#include <X11/Xlib.h>
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <thread>

void require(bool condition, const char *message) {
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        std::exit(1);
    }
}
template <class Predicate> void wait_until(Predicate condition, const char *message) {
    auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(3);
    while (!condition()) {
        require(std::chrono::steady_clock::now() < deadline, message);
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
}
int main() {
    const char *expected = std::getenv("PPWM_TEST_DISPLAY"), *actual = std::getenv("DISPLAY");
    require(expected && actual && std::strcmp(expected, actual) == 0, "use nested-test.py");
    Display *display = XOpenDisplay(nullptr);
    require(display, "display");
    Window root = DefaultRootWindow(display);
    const auto selection_name = "_NET_SYSTEM_TRAY_S" + std::to_string(DefaultScreen(display));
    Atom selection = XInternAtom(display, selection_name.c_str(), False);
    Atom opcode = XInternAtom(display, "_NET_SYSTEM_TRAY_OPCODE", False);
    Atom info = XInternAtom(display, "_XEMBED_INFO", False);
    Atom xembed = XInternAtom(display, "_XEMBED", False);
    Window owner = XGetSelectionOwner(display, selection);
    require(owner != None, "integrated tray owns selection");
    auto parent_of = [&](Window window) {
        Window root_return, parent, *children = nullptr;
        unsigned count = 0;
        XQueryTree(display, window, &root_return, &parent, &children, &count);
        if (children)
            XFree(children);
        return parent;
    };
    auto attributes = [&](Window window) {
        XWindowAttributes result{};
        XGetWindowAttributes(display, window, &result);
        return result;
    };
    auto set_mapped = [&](Window window, bool mapped) {
        long values[]{0, mapped ? 1L : 0L};
        XChangeProperty(display, window, info, info, 32, PropModeReplace,
                        reinterpret_cast<unsigned char *>(values), 2);
        XFlush(display);
    };
    auto dock = [&](Window window) {
        XEvent event{};
        event.xclient.type = ClientMessage;
        event.xclient.window = owner;
        event.xclient.message_type = opcode;
        event.xclient.format = 32;
        event.xclient.data.l[2] = static_cast<long>(window);
        XSendEvent(display, owner, False, NoEventMask, &event);
        XFlush(display);
    };
    Window first = XCreateSimpleWindow(display, root, 0, 0, 40, 40, 0, 0, 0),
           second = XCreateSimpleWindow(display, root, 0, 0, 16, 16, 0, 0, 0);
    set_mapped(first, true);
    dock(first);
    wait_until(
        [&] { return parent_of(first) == owner && attributes(first).map_state == IsViewable; },
        "icon embedded and mapped");
    bool embedded = false;
    wait_until(
        [&] {
            while (XPending(display)) {
                XEvent event;
                XNextEvent(display, &event);
                if (event.type == ClientMessage && event.xclient.message_type == xembed &&
                    event.xclient.data.l[1] == 0 &&
                    static_cast<Window>(event.xclient.data.l[3]) == owner)
                    embedded = true;
            }
            return embedded;
        },
        "XEMBED_EMBEDDED_NOTIFY");
    require(attributes(first).width == 20 && attributes(first).height == 20,
            "tray controls icon size");
    set_mapped(second, true);
    dock(second);
    wait_until([&] { return parent_of(second) == owner && attributes(owner).width == 46; },
               "two icon layout");
    dock(first);
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    require(attributes(owner).width == 46, "duplicate docking is idempotent");
    XResizeWindow(display, first, 100, 100);
    XFlush(display);
    wait_until([&] { return attributes(first).width == 20; }, "icon resize constrained");
    set_mapped(first, false);
    wait_until(
        [&] { return attributes(first).map_state == IsUnmapped && attributes(owner).width == 24; },
        "XEmbed mapped flag hides icon and releases space");
    set_mapped(first, true);
    wait_until(
        [&] { return attributes(first).map_state == IsViewable && attributes(owner).width == 46; },
        "XEmbed mapped flag restores icon");
    XDestroyWindow(display, second);
    XFlush(display);
    wait_until([&] { return attributes(owner).width == 24; }, "destroy releases tray slot");
    // A replacement manager must be able to take ownership without killing our icon.
    Window replacement = XCreateSimpleWindow(display, root, 0, 0, 1, 1, 0, 0, 0);
    XSetSelectionOwner(display, selection, replacement, CurrentTime);
    XFlush(display);
    wait_until([&] { return parent_of(first) == root; }, "selection loss detaches live icon");
    require(XGetSelectionOwner(display, selection) == replacement,
            "old tray does not clear new owner");
    XDestroyWindow(display, first);
    XDestroyWindow(display, replacement);
    XCloseDisplay(display);
    std::cout << "Tray integration passed\n";
}
