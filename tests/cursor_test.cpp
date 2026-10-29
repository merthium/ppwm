#include <X11/Xlib.h>
#include <X11/cursorfont.h>
#include <X11/extensions/Xfixes.h>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <vector>
struct CursorImage {
    unsigned width, height, xhot, yhot;
    std::vector<unsigned long> pixels;
    bool operator==(const CursorImage &) const = default;
};
CursorImage snapshot(Display *display) {
    auto *image = XFixesGetCursorImage(display);
    if (!image)
        std::exit(1);
    CursorImage result{image->width,
                       image->height,
                       image->xhot,
                       image->yhot,
                       {image->pixels, image->pixels + image->width * image->height}};
    XFree(image);
    return result;
}
int main() {
    const char *expected = std::getenv("PPWM_TEST_DISPLAY"), *actual = std::getenv("DISPLAY");
    if (!expected || !actual || std::strcmp(expected, actual))
        return 1;
    Display *display = XOpenDisplay(nullptr);
    if (!display)
        return 1;
    Window root = DefaultRootWindow(display);
    // Root and bar must inherit the normal arrow, not the X server's default cross.
    XWarpPointer(display, None, root, 0, 0, 0, 0, 1000, 760);
    XSync(display, False);
    const auto normal = snapshot(display);
    XSetWindowAttributes attributes{};
    attributes.override_redirect = True;
    attributes.cursor = XCreateFontCursor(display, XC_left_ptr);
    Window reference = XCreateWindow(display, root, 0, 0, 40, 40, 0, CopyFromParent, InputOutput,
                                     CopyFromParent, CWOverrideRedirect | CWCursor, &attributes);
    XMapRaised(display, reference);
    XWarpPointer(display, None, reference, 0, 0, 0, 0, 20, 20);
    XSync(display, False);
    const bool matches = normal == snapshot(display);
    XDestroyWindow(display, reference);
    XFreeCursor(display, attributes.cursor);
    XCloseDisplay(display);
    if (!matches) {
        std::cerr << "FAIL: root/bar must use the normal arrow cursor\n";
        return 1;
    }
    std::cout << "Root/bar cursor integration passed\n";
}
