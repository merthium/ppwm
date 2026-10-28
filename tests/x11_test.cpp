#include <X11/Xatom.h>
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <thread>
void wait() {
    std::this_thread::sleep_for(std::chrono::milliseconds(150));
}
void check(bool v, const char *m) {
    if (!v) {
        std::cerr << "FAIL: " << m << '\n';
        std::exit(1);
    }
}
int main() {
    const char *expected = std::getenv("PPWM_TEST_DISPLAY");
    const char *display = std::getenv("DISPLAY");
    check(expected && display && std::strcmp(expected, display) == 0,
          "use scripts/nested-test.py; refusing a non-test display");
    Display *d = XOpenDisplay(nullptr);
    check(d, "display");
    Window root = DefaultRootWindow(d);
    check(std::system("xdotool mousemove --sync 1000 760") == 0, "park pointer above clients");
    auto atom = [&](const char *s) { return XInternAtom(d, s, False); };
    auto value = [&](Window w, const char *s) {
        Atom type;
        int fmt;
        unsigned long n, left;
        unsigned char *data = nullptr;
        unsigned long v = 0;
        XGetWindowProperty(d, w, atom(s), 0, 1, False, AnyPropertyType, &type, &fmt, &n, &left,
                           &data);
        if (data && n)
            v = *reinterpret_cast<unsigned long *>(data);
        if (data)
            XFree(data);
        return v;
    };
    check(value(root, "_NET_SUPPORTING_WM_CHECK") != 0, "ppwm owns nested display");
    Window rr, parent, *children = nullptr, bar = None;
    unsigned children_count = 0;
    XQueryTree(d, root, &rr, &parent, &children, &children_count);
    for (unsigned i = 0; i < children_count; ++i) {
        char *name = nullptr;
        if (XFetchName(d, children[i], &name) && name) {
            if (std::strcmp(name, "ppwm-bar") == 0)
                bar = children[i];
            XFree(name);
        }
    }
    if (children)
        XFree(children);
    check(bar != None, "integrated bar exists");
    auto bar_hash = [&]() {
        XWindowAttributes attrs{};
        XGetWindowAttributes(d, bar, &attrs);
        XImage *img = XGetImage(d, bar, 0, 0, static_cast<unsigned>(attrs.width),
                                static_cast<unsigned>(attrs.height), AllPlanes, ZPixmap);
        check(img, "bar image");
        unsigned long hash = 5381;
        for (int y = 0; y < attrs.height; ++y)
            for (int x = 0; x < attrs.width; ++x)
                hash = hash * 33 + XGetPixel(img, x, y);
        XDestroyImage(img);
        return hash;
    };
    auto send = [&](Window w, const char *type, long a, long b = 0) {
        XEvent e{};
        e.xclient.type = ClientMessage;
        e.xclient.window = w;
        e.xclient.message_type = atom(type);
        e.xclient.format = 32;
        e.xclient.data.l[0] = a;
        e.xclient.data.l[1] = b;
        XSendEvent(d, root, False, SubstructureRedirectMask | SubstructureNotifyMask, &e);
        XSync(d, False);
        wait();
    };
    Window a = XCreateSimpleWindow(d, root, 50, 50, 200, 150, 0, 0, 0),
           b = XCreateSimpleWindow(d, root, 80, 80, 200, 150, 0, 0, 0);
    XStoreName(d, a, "ppwm-test-a");
    XStoreName(d, b, "ppwm-test-b");
    XMapWindow(d, a);
    XMapWindow(d, b);
    XSync(d, False);
    wait();
    XWindowAttributes aa{}, bb{};
    XGetWindowAttributes(d, a, &aa);
    XGetWindowAttributes(d, b, &bb);
    check(aa.map_state == IsViewable && bb.map_state == IsViewable, "mapped clients");
    check(aa.x + aa.width <= bb.x || bb.x + bb.width <= aa.x, "automatic nonoverlapping tiling");
    check(aa.y == 0 && bb.y == 0 && aa.height + 2 * aa.border_width <= 744,
          "bottom bar reserves work area with zero gaps");
    XWindowAttributes bar_attr{};
    XGetWindowAttributes(d, bar, &bar_attr);
    check(bar_attr.y == 744 && bar_attr.height == 24, "dwm bottom bar position");
    auto before = bar_hash();
    XStoreName(d, b, "periodic title refresh");
    XSync(d, False);
    std::this_thread::sleep_for(std::chrono::milliseconds(2200));
    check(bar_hash() != before, "timer updates bar title");
    auto above = [&](Window upper, Window lower) {
        Window rr, parent, *children = nullptr;
        unsigned count = 0;
        int up = -1, down = -1;
        XQueryTree(d, root, &rr, &parent, &children, &count);
        for (unsigned i = 0; i < count; ++i) {
            if (children[i] == upper)
                up = static_cast<int>(i);
            if (children[i] == lower)
                down = static_cast<int>(i);
        }
        if (children)
            XFree(children);
        return up > down && down >= 0;
    };
    send(a, "_NET_ACTIVE_WINDOW", 1);
    check(std::system("xdotool key --clearmodifiers alt+m") == 0, "inject layout binding");
    wait();
    check(value(root, "_NET_ACTIVE_WINDOW") == a && above(a, b),
          "monocle selected client is above other tile");
    check(std::system("xdotool key --clearmodifiers alt+t") == 0, "restore tiling");
    wait();
    check(std::system("xdotool key --clearmodifiers alt+shift+space") == 0, "floating binding");
    wait();
    XMoveResizeWindow(d, a, 120, 140, 300, 220);
    XSync(d, False);
    wait();
    XGetWindowAttributes(d, a, &aa);
    if (aa.x != 120 || aa.y != 140 || aa.width != 300 || aa.height != 220)
        std::cerr << "floating geometry: " << aa.x << "," << aa.y << " " << aa.width << "x"
                  << aa.height << " active=" << value(root, "_NET_ACTIVE_WINDOW")
                  << " expected=" << a << "\n";
    check(aa.x == 120 && aa.y == 140 && aa.width == 300 && aa.height == 220,
          "floating accepts geometry");
    check(std::system("xdotool key --clearmodifiers alt+shift+space") == 0, "return to tile");
    wait();
    XGetWindowAttributes(d, a, &aa);
    check(aa.y < 140, "return to autotiling");
    check(std::system("xdotool key --clearmodifiers alt+shift+space") == 0, "restore floating");
    wait();
    XGetWindowAttributes(d, a, &aa);
    check(aa.x == 120 && aa.y == 140 && aa.width == 300 && aa.height == 220,
          "floating geometry survives toggles");
    check(std::system("xdotool mousemove --sync 200 200 keydown Alt_L mousedown 1 mousemove --sync "
                      "250 250 mouseup 1 keyup Alt_L mousemove --sync 1000 760") == 0,
          "mouse move binding");
    wait();
    XGetWindowAttributes(d, a, &aa);
    check(aa.x == 170 && aa.y == 190, "Alt drag moves floating client");
    check(std::system("xdotool mousemove --sync 220 220 keydown Alt_L mousedown 3 mousemove --sync "
                      "280 260 mouseup 3 keyup Alt_L mousemove --sync 1000 760") == 0,
          "mouse resize binding");
    wait();
    XGetWindowAttributes(d, a, &aa);
    check(aa.width == 360 && aa.height == 260, "Alt right drag resizes client");
    check(std::system("xdotool key --clearmodifiers alt+h") == 0, "hide binding");
    wait();
    XGetWindowAttributes(d, a, &aa);
    check(aa.map_state == IsUnmapped && value(a, "WM_STATE") == IconicState,
          "hide retains tracked iconic window");
    check(std::system("xdotool key --clearmodifiers alt+shift+s") == 0, "show all binding");
    wait();
    XGetWindowAttributes(d, a, &aa);
    check(aa.map_state == IsViewable, "show all restores window");
    check(std::system("xdotool key --clearmodifiers alt+b") == 0, "toggle bar off");
    wait();
    XGetWindowAttributes(d, bar, &bar_attr);
    XGetWindowAttributes(d, b, &bb);
    check(bar_attr.map_state == IsUnmapped && bb.height + 2 * bb.border_width == 768,
          "hidden bar releases screen area");
    check(std::system("xdotool key --clearmodifiers alt+b") == 0, "toggle bar on");
    wait();
    send(b, "_NET_WM_DESKTOP", 1);
    XGetWindowAttributes(d, b, &bb);
    check(bb.map_state == IsUnmapped, "tag assignment hides client");
    send(root, "_NET_CURRENT_DESKTOP", 1);
    XGetWindowAttributes(d, b, &bb);
    check(bb.map_state == IsViewable, "tag switch remaps tracked client");
    send(b, "_NET_WM_STATE", 1, static_cast<long>(atom("_NET_WM_STATE_FULLSCREEN")));
    XGetWindowAttributes(d, b, &bb);
    check(bb.x == 0 && bb.y == 0 && bb.width == DisplayWidth(d, DefaultScreen(d)) &&
              bb.border_width == 0,
          "fullscreen geometry");
    send(a, "_NET_WM_DESKTOP", 1);
    send(a, "_NET_ACTIVE_WINDOW", 1);
    check(value(root, "_NET_ACTIVE_WINDOW") == b && above(b, a),
          "fullscreen keeps focus and stacking");
    send(b, "_NET_WM_STATE", 0, static_cast<long>(atom("_NET_WM_STATE_FULLSCREEN")));
    XGetWindowAttributes(d, b, &bb);
    check(bb.height + 2 * bb.border_width <= 744 && bb.border_width == 2, "exit fullscreen");
    send(a, "_NET_WM_DESKTOP", 0);
    send(a, "_NET_WM_STATE", 1, static_cast<long>(atom("_NET_WM_STATE_FULLSCREEN")));
    send(b, "_NET_WM_STATE", 1, static_cast<long>(atom("_NET_WM_STATE_FULLSCREEN")));
    check(std::system("xdotool key --clearmodifiers alt+0") == 0, "view all fullscreen tags");
    wait();
    const Window fullscreen_focus = value(root, "_NET_ACTIVE_WINDOW");
    check((fullscreen_focus == a && above(a, b)) ||
              (fullscreen_focus == b && above(b, a)),
          "combined tags keep focused fullscreen above other fullscreen windows");
    send(root, "_NET_CURRENT_DESKTOP", 1);
    send(a, "_NET_WM_DESKTOP", 1);
    check(value(b, "_NET_WM_STATE") == 0 &&
              value(a, "_NET_WM_STATE") == atom("_NET_WM_STATE_FULLSCREEN"),
          "moving fullscreen resolves tag conflict");
    check(value(root, "_NET_ACTIVE_WINDOW") == a && above(a, b),
          "fullscreen migration preserves visible focus");
    check(std::system("xdotool key --clearmodifiers alt+h") == 0, "hide fullscreen");
    wait();
    check(value(root, "_NET_ACTIVE_WINDOW") != a, "hidden fullscreen releases focus");
    check(std::system("xdotool key --clearmodifiers alt+shift+s") == 0,
          "restore hidden fullscreen");
    wait();
    check(value(root, "_NET_ACTIVE_WINDOW") == a && above(a, b),
          "restored fullscreen regains focus");
    send(a, "_NET_WM_STATE", 0, static_cast<long>(atom("_NET_WM_STATE_FULLSCREEN")));
    send(a, "_NET_ACTIVE_WINDOW", 1);
    check(std::system("xdotool key --clearmodifiers alt+h") == 0, "hide before client remap");
    wait();
    XMapWindow(d, a);
    XSync(d, False);
    wait();
    XGetWindowAttributes(d, a, &aa);
    check(aa.map_state == IsViewable && value(a, "WM_STATE") == NormalState,
          "client MapRequest restores explicitly hidden client");
    XUnmapWindow(d, b);
    XSync(d, False);
    wait();
    check(value(b, "WM_STATE") == 0, "withdraw updates WM_STATE");
    XDestroyWindow(d, a);
    XDestroyWindow(d, b);
    XSync(d, False);
    wait();
    XCloseDisplay(d);
    std::cout << "X11 integration passed\n";
}
