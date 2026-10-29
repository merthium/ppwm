#include <X11/Xlib.h>
#include <iostream>
#include <unistd.h>
// An unmapped parent keeps host pointer/keyboard events out of automated Xephyr tests.
int main() {
    Display *display = XOpenDisplay(nullptr);
    if (!display)
        return 1;
    const Window window =
        XCreateSimpleWindow(display, DefaultRootWindow(display), 0, 0, 1024, 768, 0, 0, 0);
    XSync(display, False);
    std::cout << window << std::endl;
    char stop;
    while (read(STDIN_FILENO, &stop, 1) > 0) {
    }
    XDestroyWindow(display, window);
    XCloseDisplay(display);
}
