#pragma once

#ifdef __linux__

// The Xlib types the probe needs (XErrorEvent, XErrorHandler,
// XSetErrorHandler).
//
// Xlib is included directly rather than through the repo's include/x11.h
// wrapper: that wrapper #undefs True, False, ButtonPress, KeyPress and
// friends to keep them from colliding with Qt and gtest, and the file
// that uses this probe (X11Adapter.cpp) uses exactly those Xlib macros.
// Including the wrapper here would undefine them for the whole translation
// unit.
#include <X11/X.h>
#include <X11/Xlib.h>

namespace havel {

// Scoped X11 error probe.
//
// XSelectInput() returns void: the only way to learn that the server
// refused the request is through the error handler. The probe installs a
// handler for the duration of one call, records the error code, and then
// restores the previous handler.
//
// The previous handler is DisplayManager's (or X11IOBackend's), which logs
// the error. Restoring it - rather than replacing it permanently - keeps
// that logging intact for every other X11 call on the shared connection.
//
// The handler is global, not per-display, so exactly one probe may be
// active at a time. The callers all hold the probe on the stack of the
// thread that makes the X11 call, and they never nest two probes.
class ScopedX11ErrorProbe {
public:
  explicit ScopedX11ErrorProbe(Display *) {
    previous_ = XSetErrorHandler(&Probe);
    failed_ = false;
    errorCode_ = 0;
    active_ = this;
  }

  ~ScopedX11ErrorProbe() {
    XSetErrorHandler(previous_);
    // Clear only OUR OWN registration: if another probe was installed
    // after this one, clearing unconditionally would disarm it.
    if (active_ == this)
      active_ = nullptr;
  }

  ScopedX11ErrorProbe(const ScopedX11ErrorProbe &) = delete;
  ScopedX11ErrorProbe &operator=(const ScopedX11ErrorProbe &) = delete;

  bool Failed() const { return failed_; }
  int ErrorCode() const { return errorCode_; }

private:
  static int Probe(Display *, XErrorEvent *event) {
    if (active_) {
      active_->failed_ = true;
      active_->errorCode_ = event ? event->error_code : 0;
    }
    return 0;
  }

  XErrorHandler previous_ = nullptr;
  bool failed_ = false;
  int errorCode_ = 0;

  static inline ScopedX11ErrorProbe *active_ = nullptr;
};

} // namespace havel

#endif // __linux__
