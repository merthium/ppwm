#include <X11/Xatom.h>
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <stdexcept>
#include <thread>
namespace {
void require(bool condition, const char *message) {
    if (!condition)
        throw std::runtime_error(message);
}
template <class Predicate> void wait_until(Predicate predicate, const char *message) {
    const auto end = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    while (!predicate()) {
        require(std::chrono::steady_clock::now() < end, message);
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
}
unsigned long value(Display *d, Window w, const char *name) {
    Atom type{};
    int format{};
    unsigned long count{}, remaining{};
    unsigned char *data = nullptr;
    XGetWindowProperty(d, w, XInternAtom(d, name, False), 0, 1, False, AnyPropertyType, &type,
                       &format, &count, &remaining, &data);
    const unsigned long result =
        data && count && format == 32 ? *reinterpret_cast<unsigned long *>(data) & 0xffffffffUL : 0;
    if (data)
        XFree(data);
    return result;
}
XWindowAttributes attributes(Display *d, Window w) {
    XWindowAttributes a{};
    require(XGetWindowAttributes(d, w, &a), "read attributes");
    return a;
}
void send(Display *d, Window w, const char *name, long argument) {
    XEvent e{};
    e.xclient.type = ClientMessage;
    e.xclient.window = w;
    e.xclient.message_type = XInternAtom(d, name, False);
    e.xclient.format = 32;
    e.xclient.data.l[0] = argument;
    XSendEvent(d, DefaultRootWindow(d), False, SubstructureRedirectMask | SubstructureNotifyMask,
               &e);
    XFlush(d);
}
void key(const char *binding) {
    const auto command = std::string("xdotool key --clearmodifiers ") + binding;
    require(std::system(command.c_str()) == 0, "inject key");
}
void run(Display *d) {
    Window root = DefaultRootWindow(d);
    send(d, root, "_NET_CURRENT_DESKTOP", 0);
    wait_until([&] { return value(d, root, "_NET_CURRENT_DESKTOP") == 0; }, "start on first tag");
    const auto create = [&] {
        Window w = XCreateSimpleWindow(d, root, 20, 20, 200, 100, 0, 0, 0);
        XMapWindow(d, w);
        XFlush(d);
        wait_until([&] { return value(d, w, "WM_STATE") == NormalState; }, "manage client");
        return w;
    };
    const Window a = create(), b = create();
    send(d, a, "_NET_ACTIVE_WINDOW", 1);
    wait_until([&] { return value(d, root, "_NET_ACTIVE_WINDOW") == a; }, "select floating client");
    key("alt+shift+space");
    XMoveResizeWindow(d, a, 123, 145, 301, 221);
    XFlush(d);
    wait_until(
        [&] {
            const auto x = attributes(d, a);
            return x.x == 123 && x.y == 145 && x.width == 301 && x.height == 221;
        },
        "set floating geometry");
    send(d, b, "_NET_WM_DESKTOP", 1);
    send(d, b, "_NET_ACTIVE_WINDOW", 1);
    wait_until([&] { return value(d, root, "_NET_ACTIVE_WINDOW") == b; },
               "select second tag client");
    key("alt+h");
    wait_until([&] { return value(d, b, "WM_STATE") == IconicState; }, "hide second tag client");
    const Window c = create();
    send(d, root, "_NET_CURRENT_DESKTOP", 0);
    wait_until([&] { return attributes(d, c).map_state == IsUnmapped; }, "return to tag one");
    key("alt+ctrl+2");
    wait_until(
        [&] {
            return attributes(d, a).map_state == IsViewable &&
                   attributes(d, c).map_state == IsViewable;
        },
        "combine tags");
    send(d, root, "_NET_CURRENT_DESKTOP", 2);
    wait_until([&] { return value(d, root, "_NET_CURRENT_DESKTOP") == 2; }, "select settings tag");
    key("alt+b");
    key("alt+Tab");
    wait_until([&] { return attributes(d, a).map_state == IsViewable; }, "restore combined view");
    send(d, a, "_NET_ACTIVE_WINDOW", 1);
    wait_until([&] { return value(d, root, "_NET_ACTIVE_WINDOW") == a; }, "select before restart");
    XSelectInput(d, root, PropertyChangeMask);
    XSync(d, False);
    while (XPending(d)) {
        XEvent e;
        XNextEvent(d, &e);
    }
    key("alt+ctrl+shift+q");
    bool removed = false, installed = false;
    const Atom owner = XInternAtom(d, "_NET_SUPPORTING_WM_CHECK", False);
    wait_until(
        [&] {
            while (XPending(d)) {
                XEvent e;
                XNextEvent(d, &e);
                if (e.type == PropertyNotify && e.xproperty.atom == owner) {
                    if (e.xproperty.state == PropertyDelete)
                        removed = true;
                    else if (removed)
                        installed = true;
                }
            }
            return installed;
        },
        "new WM instance published after restart");
    wait_until(
        [&] {
            const auto x = attributes(d, a);
            return value(d, root, "_NET_CURRENT_DESKTOP") == 0 &&
                   value(d, a, "_NET_WM_DESKTOP") == 0 && value(d, b, "_NET_WM_DESKTOP") == 1 &&
                   value(d, b, "WM_STATE") == IconicState &&
                   attributes(d, b).map_state == IsUnmapped &&
                   attributes(d, c).map_state == IsViewable && x.x == 123 && x.y == 145 &&
                   x.width == 301 && x.height == 221 && value(d, root, "_NET_ACTIVE_WINDOW") == a;
        },
        "restart preserves combined tags, hidden state, floating geometry and focus");
    key("alt+Tab");
    wait_until(
        [&] {
            return value(d, root, "_NET_CURRENT_DESKTOP") == 2 &&
                   attributes(d, a).map_state == IsUnmapped;
        },
        "previous view survives restart");
    const Window probe = create();
    wait_until(
        [&] { return attributes(d, probe).height + 2 * attributes(d, probe).border_width == 768; },
        "per-tag hidden bar survives restart");
    XDestroyWindow(d, probe);
    XFlush(d);
    key("alt+b"); // Restore fixture settings for following tests.
    key("alt+Tab");
    XDestroyWindow(d, a);
    XDestroyWindow(d, b);
    XDestroyWindow(d, c);
    XFlush(d);
    send(d, root, "_NET_CURRENT_DESKTOP", 0);
    std::cout << "Restart session preservation passed\n";
}
} // namespace
int main() {
    const char *expected = std::getenv("PPWM_TEST_DISPLAY"), *actual = std::getenv("DISPLAY");
    if (!expected || !actual || std::strcmp(expected, actual))
        return 1;
    Display *d = XOpenDisplay(nullptr);
    if (!d)
        return 1;
    try {
        run(d);
        XCloseDisplay(d);
    } catch (const std::exception &error) {
        std::cerr << "FAIL: " << error.what() << '\n';
        XCloseDisplay(d);
        return 1;
    }
}
