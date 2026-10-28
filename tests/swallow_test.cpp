#include <X11/Xatom.h>
#include <X11/Xlib.h>
#include <X11/Xutil.h>

#include <cerrno>
#include <chrono>
#include <csignal>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <memory>
#include <poll.h>
#include <stdexcept>
#include <string>
#include <sys/prctl.h>
#include <sys/wait.h>
#include <thread>
#include <unistd.h>

namespace {
void require(bool condition, const char* message) {
    if (!condition)
        throw std::runtime_error(message);
}

template <class Predicate> void wait_until(Predicate condition, const char* message) {
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    while (!condition()) {
        require(std::chrono::steady_clock::now() < deadline, message);
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
}

using DisplayOwner = std::unique_ptr<Display, decltype(&XCloseDisplay)>;

struct Child {
    pid_t pid = -1;
    int pipe_read = -1;
    ~Child() {
        stop();
        if (pipe_read >= 0)
            close(pipe_read);
    }
    void stop() {
        if (pid > 0) {
            kill(pid, SIGKILL);
            while (waitpid(pid, nullptr, 0) < 0 && errno == EINTR) {}
            pid = -1;
        }
    }
};

XWindowAttributes attributes(Display* display, Window window) {
    XWindowAttributes result{};
    require(XGetWindowAttributes(display, window, &result), "read window attributes");
    return result;
}

bool same_geometry(const XWindowAttributes& a, const XWindowAttributes& b) {
    return a.x == b.x && a.y == b.y && a.width == b.width && a.height == b.height &&
           a.border_width == b.border_width;
}

unsigned long property(Display* display, Window window, const char* name) {
    Atom type = None;
    int format = 0;
    unsigned long count = 0, remaining = 0;
    unsigned char* data = nullptr;
    const int status = XGetWindowProperty(display, window, XInternAtom(display, name, False),
                                          0, 1, False, AnyPropertyType, &type, &format,
                                          &count, &remaining, &data);
    const auto free_data = [](unsigned char* p) { XFree(p); };
    const std::unique_ptr<unsigned char, decltype(free_data)> owner(data, free_data);
    if (status != Success || format != 32 || count != 1 || !data)
        return ~0UL;
    return *reinterpret_cast<unsigned long*>(data);
}

void send(Display* display, Window window, const char* name, long value) {
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

Window create_window(Display* display, const char* name, bool terminal) {
    Window window = XCreateSimpleWindow(display, DefaultRootWindow(display),
                                         20, 30, 240, 180, 0, 0, 0);
    XStoreName(display, window, name);
    char instance[] = "ppwm-swallow-test";
    char terminal_class[] = "URxvt";
    char application_class[] = "PpwmSwallowTest";
    XClassHint hint{instance, terminal ? terminal_class : application_class};
    XSetClassHint(display, window, &hint);
    XMapWindow(display, window);
    XFlush(display);
    return window;
}

int child_main(int pipe_write) {
    DisplayOwner display(XOpenDisplay(nullptr), XCloseDisplay);
    require(static_cast<bool>(display), "child display connection");
    const Window window = create_window(display.get(), "ppwm-swallow-child", false);
    XSync(display.get(), False);
    require(write(pipe_write, &window, sizeof(window)) == static_cast<ssize_t>(sizeof(window)),
            "report child window");
    close(pipe_write);
    for (;;)
        pause();
}

void run_test(Display* display) {
    const Window root = DefaultRootWindow(display);
    const auto original_desktop = property(display, root, "_NET_CURRENT_DESKTOP");
    require(original_desktop < 9, "ppwm publishes current desktop");
    // Both windows deliberately share one connection and PID, as urxvtd windows do.
    const Window first = create_window(display, "ppwm-swallow-terminal-first", true);
    wait_until([&] { return property(display, first, "WM_STATE") == NormalState; },
               "first terminal managed");
    const Window second = create_window(display, "ppwm-swallow-terminal-second", true);
    wait_until([&] { return property(display, second, "WM_STATE") == NormalState; },
               "second terminal managed");
    send(display, first, "_NET_WM_DESKTOP", 1);
    send(display, second, "_NET_WM_DESKTOP", 1);
    send(display, root, "_NET_CURRENT_DESKTOP", 1);
    wait_until([&] {
        const auto a = attributes(display, first);
        const auto b = attributes(display, second);
        return a.map_state == IsViewable && b.map_state == IsViewable &&
               property(display, first, "_NET_WM_DESKTOP") == 1 &&
               property(display, second, "_NET_WM_DESKTOP") == 1 &&
               (a.x + a.width <= b.x || b.x + b.width <= a.x);
    }, "both terminals tiled on desktop two");
    const auto first_before = attributes(display, first);
    const auto second_before = attributes(display, second);
    const auto second_desktop = property(display, second, "_NET_WM_DESKTOP");

    int pipe_fds[2];
    require(pipe(pipe_fds) == 0, "create child pipe");
    Child child;
    child.pipe_read = pipe_fds[0];
    const pid_t parent_pid = getpid();
    child.pid = fork();
    if (child.pid < 0) {
        close(pipe_fds[1]);
        throw std::runtime_error("fork child");
    }
    if (child.pid == 0) {
        // Also clean up if the harness kills this test before its RAII guards run.
        if (prctl(PR_SET_PDEATHSIG, SIGKILL) != 0 || getppid() != parent_pid)
            _exit(126);
        close(pipe_fds[0]);
        close(ConnectionNumber(display));
        const auto id = std::to_string(second);
        const auto descriptor = std::to_string(pipe_fds[1]);
        if (setenv("WINDOWID", id.c_str(), 1) != 0)
            _exit(126);
        execl("/proc/self/exe", "swallow_test", "--child", descriptor.c_str(), nullptr);
        _exit(127);
    }
    close(pipe_fds[1]);
    pollfd ready{child.pipe_read, POLLIN, 0};
    require(poll(&ready, 1, 5000) == 1 && (ready.revents & POLLIN), "child reports window");
    Window application = None;
    require(read(child.pipe_read, &application, sizeof(application)) ==
                static_cast<ssize_t>(sizeof(application)), "read child window ID");
    wait_until([&] {
        return attributes(display, second).map_state == IsUnmapped &&
               attributes(display, application).map_state == IsViewable;
    }, "WINDOWID selects second terminal for swallowing");
    wait_until([&] {
        return same_geometry(attributes(display, application), second_before) &&
               property(display, application, "_NET_WM_DESKTOP") == second_desktop;
    }, "child inherits terminal geometry and desktop");
    require(attributes(display, first).map_state == IsViewable &&
                same_geometry(attributes(display, first), first_before),
            "same-PID first terminal stays visible and unchanged");
    child.stop();
    wait_until([&] {
        return attributes(display, second).map_state == IsViewable &&
               same_geometry(attributes(display, second), second_before);
    }, "terminal restores after child exits");
    require(property(display, second, "_NET_WM_DESKTOP") == second_desktop,
            "restored terminal keeps desktop");
    require(attributes(display, first).map_state == IsViewable &&
                same_geometry(attributes(display, first), first_before),
            "first terminal stays unchanged after restoration");
    XDestroyWindow(display, first);
    XDestroyWindow(display, second);
    send(display, root, "_NET_CURRENT_DESKTOP", static_cast<long>(original_desktop));
    XSync(display, False);
}
} // namespace

int main(int argc, char** argv) {
    try {
        const char* expected = std::getenv("PPWM_TEST_DISPLAY");
        const char* actual = std::getenv("DISPLAY");
        require(expected && actual && std::strcmp(expected, actual) == 0,
                "use scripts/nested-test.py; refusing a non-test display");
        if (argc == 3 && std::strcmp(argv[1], "--child") == 0)
            return child_main(std::stoi(argv[2]));
        DisplayOwner display(XOpenDisplay(nullptr), XCloseDisplay);
        require(static_cast<bool>(display), "display connection");
        run_test(display.get());
        std::cout << "Terminal swallowing integration passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "FAIL: " << error.what() << '\n';
        return 1;
    }
}
