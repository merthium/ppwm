#include "process.hpp"

#include <cstdlib>
#include <iostream>
#include <string_view>
#include <sys/wait.h>
#include <unistd.h>

namespace {
void check(bool condition, const char* message) {
    if (!condition) {
        std::cerr << message << '\n';
        std::exit(1);
    }
}
}

int main(int argc, char** argv) {
    const ppwm::ProcessLookup lookup(nullptr);
    if (argc == 2 && std::string_view(argv[1]) == "--ancestor") {
        check(lookup.terminal_window(getpid()) == 424242, "ancestor WINDOWID lookup");
        return 0;
    }
    if (argc == 2 && std::string_view(argv[1]) == "--environment") {
        check(lookup.terminal_window(getpid()) == 424242, "exact WINDOWID environment lookup");
        const pid_t child = fork();
        check(child >= 0, "fork ancestor test");
        if (child == 0) {
            char executable[] = "/proc/self/exe";
            char mode[] = "--ancestor";
            char* arguments[] = {executable, mode, nullptr};
            char* environment[] = {nullptr};
            execve(executable, arguments, environment);
            _exit(127);
        }
        int status = 0;
        check(waitpid(child, &status, 0) == child && WIFEXITED(status) && WEXITSTATUS(status) == 0,
              "child without WINDOWID finds parent terminal");
        return 0;
    }

    using ppwm::detail::parse_window_id;
    check(parse_window_id("42") == 42, "decimal window ID");
    check(parse_window_id("0xabcdef") == 0xabcdef, "hexadecimal window ID");
    check(parse_window_id("4294967295") == 0xffffffffUL, "maximum window ID");
    check(parse_window_id("0XFFFFFFFF") == 0xffffffffUL, "maximum hex window ID");
    check(parse_window_id("00042") == 42, "leading zero remains decimal");
    for (std::string_view invalid : {"", "0", "0x0", "-1", "+1", " 42", "42 ",
                                     "42junk", "0x", "0xg", "4294967296", "0x100000000",
                                     "9999999999999999999999999999"})
        check(!parse_window_id(invalid), "invalid window ID accepted");
    check(!lookup.window_pid(42), "unavailable XRes returns no PID");
    check(!lookup.terminal_window(0), "zero PID rejected");
    check(!lookup.terminal_window(-1), "negative PID rejected");
    const pid_t child = fork();
    check(child >= 0, "fork environment test");
    if (child == 0) {
        char executable[] = "/proc/self/exe";
        char mode[] = "--environment";
        char misleading[] = "NOT_WINDOWID=999";
        char window[] = "WINDOWID=424242";
        char* arguments[] = {executable, mode, nullptr};
        char* environment[] = {misleading, window, nullptr};
        execve(executable, arguments, environment);
        _exit(127);
    }
    int status = 0;
    check(waitpid(child, &status, 0) == child && WIFEXITED(status) && WEXITSTATUS(status) == 0,
          "environment integration test");
    if (Display* display = XOpenDisplay(nullptr)) {
        const ppwm::ProcessLookup xlookup(display);
        const Window window = XCreateSimpleWindow(display, DefaultRootWindow(display),
                                                  0, 0, 1, 1, 0, 0, 0);
        XSync(display, False);
        check(xlookup.window_pid(window) == getpid(), "XRes returns local window owner PID");
        XDestroyWindow(display, window);
        XCloseDisplay(display);
    } else {
        std::cout << "XRes live test skipped (no X display)\n";
    }
    std::cout << "process lookup tests passed\n";
}
