#include <X11/Xatom.h>
#include <X11/Xlib.h>
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <thread>
#include <vector>

int main() {
    const char *expected = std::getenv("PPWM_TEST_DISPLAY"), *actual = std::getenv("DISPLAY");
    if (!expected || !actual || std::strcmp(expected, actual))
        return 1;
    Display *d = XOpenDisplay(nullptr);
    if (!d)
        return 1;
    const Window root = DefaultRootWindow(d);
    const Window window = XCreateSimpleWindow(d, root, 20, 20, 200, 100, 0, 0, 0);
    const Atom state = XInternAtom(d, "_NET_WM_STATE", False);
    const Atom skip_taskbar = XInternAtom(d, "_NET_WM_STATE_SKIP_TASKBAR", False);
    const Atom fullscreen = XInternAtom(d, "_NET_WM_STATE_FULLSCREEN", False);
    XChangeProperty(d, window, state, XA_ATOM, 32, PropModeReplace,
                    reinterpret_cast<const unsigned char *>(&skip_taskbar), 1);
    XMapWindow(d, window);
    XFlush(d);
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    auto states = [&] {
        Atom type{};
        int format{};
        unsigned long count{}, remaining{};
        unsigned char *raw = nullptr;
        XGetWindowProperty(d, window, state, 0, 64, False, XA_ATOM, &type, &format, &count,
                           &remaining, &raw);
        std::vector<Atom> values;
        if (raw && type == XA_ATOM && format == 32) {
            const auto *atoms = reinterpret_cast<const Atom *>(raw);
            values.assign(atoms, atoms + count);
        }
        if (raw)
            XFree(raw);
        return values;
    };
    bool passed = states() == std::vector<Atom>{skip_taskbar};
    for (long action : {1L, 0L}) {
        XEvent e{};
        e.xclient.type = ClientMessage;
        e.xclient.window = window;
        e.xclient.message_type = state;
        e.xclient.format = 32;
        e.xclient.data.l[0] = action;
        e.xclient.data.l[1] = static_cast<long>(fullscreen);
        XSendEvent(d, root, False, SubstructureRedirectMask | SubstructureNotifyMask, &e);
        XFlush(d);
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
        bool has_skip = false, has_full = false;
        for (const auto atom : states()) {
            has_skip |= atom == skip_taskbar;
            has_full |= atom == fullscreen;
        }
        if (!has_skip || has_full != (action == 1)) {
            std::cerr << "FAIL: fullscreen " << (action ? "entry" : "exit")
                      << " must preserve unrelated _NET_WM_STATE atoms (skip_taskbar=" << has_skip
                      << ", fullscreen=" << has_full << ")\n";
            passed = false;
        }
    }
    XDestroyWindow(d, window);
    XCloseDisplay(d);
    return passed ? 0 : 1;
}
