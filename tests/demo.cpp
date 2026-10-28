// Screenshot fixture: real X11 clients, intentionally independent of ppwm internals.
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

struct Panel {
    Window window{};
    std::string title;
    std::vector<std::string> lines;
    bool numbers{};
};

std::vector<std::string> read_lines(const char *path) {
    std::ifstream stream(path);
    if (!stream)
        throw std::runtime_error(std::string("Cannot open ") + path);
    std::vector<std::string> lines;
    for (std::string line; std::getline(stream, line);)
        lines.push_back(line);
    return lines;
}

int main(int argc, char **argv) {
    const char *expected = std::getenv("PPWM_DEMO_DISPLAY");
    const char *actual = std::getenv("DISPLAY");
    if (argc != 3 || !expected || !actual || std::strcmp(expected, actual) != 0) {
        std::cerr << "Run through scripts/capture-demo.py on its isolated display.\n";
        return 1;
    }
    try {
        std::vector<Panel> panels{
            {0, "src/layout.hpp  /  C++20", read_lines(argv[1]), true},
            {0, "build + test  /  captured command output", read_lines(argv[2]), false},
            {0, "ppwm  /  keyboard", {
                "A small, patchable X11 window manager.", "",
                "Alt + j / k       focus next / previous",
                "Alt + Enter       promote to master",
                "Alt + t / f / m   tile / float / monocle",
                "Alt + 1 ... 9     switch tag",
                "Alt + mouse       move / resize",
                "Alt + b           toggle bottom bar", "",
                "C++20  /  Xlib + Xft  /  source configuration",
                "Live X11 clients on an isolated Xephyr screen."}, false}};
        Display *display = XOpenDisplay(nullptr);
        if (!display)
            throw std::runtime_error("Cannot open nested display");
        const int screen = DefaultScreen(display);
        auto color = [&](const char *name) {
            XColor value{}, exact{};
            if (!XAllocNamedColor(display, DefaultColormap(display, screen), name, &value, &exact))
                throw std::runtime_error("Cannot allocate grayscale color");
            return value.pixel;
        };
        const auto background = color("#101010"), foreground = color("#cccccc"),
                   muted = color("#777777"), heading = color("#eeeeee"), rule = color("#303030");
        XFontStruct *font = XLoadQueryFont(display, "-misc-fixed-medium-r-normal--14-*-*-*-*-*-iso8859-1");
        if (!font)
            font = XLoadQueryFont(display, "fixed");
        if (!font)
            throw std::runtime_error("Cannot load X11 fixed font");
        GC gc = XCreateGC(display, DefaultRootWindow(display), 0, nullptr);
        XSetFont(display, gc, font->fid);
        const int line_height = std::max(18, font->ascent + font->descent + 3);
        // New clients become master, so map the source panel last.
        for (auto it = panels.rbegin(); it != panels.rend(); ++it) {
            auto &panel = *it;
            panel.window = XCreateSimpleWindow(display, DefaultRootWindow(display), 0, 0,
                                                640, 380, 0, background, background);
            XStoreName(display, panel.window, panel.title.c_str());
            char name[] = "ppwm-demo", klass[] = "PpwmDemo";
            XClassHint hint{name, klass};
            XSetClassHint(display, panel.window, &hint);
            XSelectInput(display, panel.window, ExposureMask | StructureNotifyMask);
            XMapWindow(display, panel.window);
        }
        XFlush(display);
        while (true) {
            XEvent event{};
            XNextEvent(display, &event);
            if (event.type != Expose && event.type != ConfigureNotify)
                continue;
            for (const auto &panel : panels) {
                if (event.xany.window != panel.window)
                    continue;
                XWindowAttributes attrs{};
                XGetWindowAttributes(display, panel.window, &attrs);
                XClearWindow(display, panel.window);
                auto text = [&](int x, int y, const std::string &value, unsigned long ink) {
                    XSetForeground(display, gc, ink);
                    XDrawString(display, panel.window, gc, x, y, value.c_str(),
                                static_cast<int>(value.size()));
                };
                text(20, 29, panel.title, heading);
                XSetForeground(display, gc, rule);
                XDrawLine(display, panel.window, gc, 20, 44, attrs.width - 20, 44);
                int y = 70;
                for (std::size_t i = 0; i < panel.lines.size() && y < attrs.height - 16; ++i) {
                    int x = 20;
                    if (panel.numbers) {
                        text(x, y, std::to_string(i + 1), muted);
                        x = 52;
                    }
                    const auto &line = panel.lines[i];
                    text(x, y, line, line.starts_with("//") || line.starts_with("    //") ? muted : foreground);
                    y += line_height;
                }
                XFlush(display);
            }
        }
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
