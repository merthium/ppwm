#pragma once
#include <X11/Xlib.h>
#include <stdexcept>

namespace ppwm {
// RAII: the connection closes even if WindowManager construction throws.
class X11Connection {
  public:
    X11Connection() : display_(XOpenDisplay(nullptr)) {
        if (!display_) {
            throw std::runtime_error("cannot open DISPLAY");
        }
    }
    ~X11Connection() {
        XCloseDisplay(display_);
    }

    X11Connection(const X11Connection &) = delete;
    X11Connection &operator=(const X11Connection &) = delete;

    [[nodiscard]] Display *get() const noexcept {
        return display_;
    }

  private:
    Display *display_;
};
} // namespace ppwm
