#pragma once

#ifdef __linux__
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/keysym.h>
#include <X11/XKBlib.h>
#include <X11/extensions/XTest.h>
#include <X11/extensions/Xrandr.h>
#include <X11/XF86keysym.h>
#endif

namespace havel {
    // Forward declarations of common X11 types to avoid conflicts
    #ifdef __linux__
    extern Display* GetDisplay();
    extern Window GetRootWindow();
    #endif
}
