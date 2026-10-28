#include "window_manager.hpp"
#include <filesystem>
#include <iostream>
#include <string_view>
#include <unistd.h>
int main(int argc, char **argv) {
    if (argc > 1) {
        if (std::string_view(argv[1]) == "--version") {
            std::cout << "ppwm 0.1.0\n";
            return 0;
        }
        if (std::string_view(argv[1]) == "--help") {
            std::cout << "ppwm: minimal X11 window manager. See README.md for bindings.\n";
            return 0;
        }
        std::cerr << "unknown option\n";
        return 2;
    }
    try {
        // Keep the pathname, not the running inode: rebuilding replaces the executable.
        const auto executable = std::filesystem::read_symlink("/proc/self/exe");
        bool restart = false;
        {
            ppwm::WindowManager window_manager;
            window_manager.run();
            restart = window_manager.restart_requested();
        }
        if (restart) {
            execl(executable.c_str(), argv[0], static_cast<char *>(nullptr));
            throw std::runtime_error("restart failed");
        }
    } catch (const std::exception &error) {
        std::cerr << "ppwm: " << error.what() << '\n';
        return 1;
    }
}
