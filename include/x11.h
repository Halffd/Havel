#pragma once
// Safe X11 wrapper - use this instead of direct X11 includes

// Include X11 headers FIRST (they will define the macros)
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/extensions/XTest.h>
#include <X11/extensions/Xrandr.h>
#include <X11/keysym.h>
#include <X11/XKBlib.h>
#include <X11/XF86keysym.h>
#include <X11/Xatom.h>

// X11 types and constants in a safe namespace
namespace havel::x11 {
    // Type aliases
    using Display = ::Display;
    using Window = ::Window;
    using XEvent = ::XEvent;
    using KeyCode = ::KeyCode;
    using KeySym = ::KeySym;
    using Atom = ::Atom;
    using Screen = ::Screen;
    using Visual = ::Visual;
    using Colormap = ::Colormap;
    using GC = ::GC;
    using XWindowAttributes = ::XWindowAttributes;
    using XSetWindowAttributes = ::XSetWindowAttributes;
    using XSizeHints = ::XSizeHints;
    using XWMHints = ::XWMHints;
    using XClassHint = ::XClassHint;
    using XTextProperty = ::XTextProperty;
    using XKeyEvent = ::XKeyEvent;
    using XButtonEvent = ::XButtonEvent;
    using XMotionEvent = ::XMotionEvent;
    using XCrossingEvent = ::XCrossingEvent;
    using XFocusChangeEvent = ::XFocusChangeEvent;
    using XExposeEvent = ::XExposeEvent;
    using XGraphicsExposeEvent = ::XGraphicsExposeEvent;
    using XNoExposeEvent = ::XNoExposeEvent;
    using XVisibilityEvent = ::XVisibilityEvent;
    using XCreateWindowEvent = ::XCreateWindowEvent;
    using XDestroyWindowEvent = ::XDestroyWindowEvent;
    using XUnmapEvent = ::XUnmapEvent;
    using XMapEvent = ::XMapEvent;
    using XMapRequestEvent = ::XMapRequestEvent;
    using XReparentEvent = ::XReparentEvent;
    using XConfigureEvent = ::XConfigureEvent;
    using XGravityEvent = ::XGravityEvent;
    using XResizeRequestEvent = ::XResizeRequestEvent;
    using XConfigureRequestEvent = ::XConfigureRequestEvent;
    using XCirculateEvent = ::XCirculateEvent;
    using XCirculateRequestEvent = ::XCirculateRequestEvent;
    using XPropertyEvent = ::XPropertyEvent;
    using XSelectionClearEvent = ::XSelectionClearEvent;
    using XSelectionRequestEvent = ::XSelectionRequestEvent;
    using XSelectionEvent = ::XSelectionEvent;
    using XColormapEvent = ::XColormapEvent;
    using XClientMessageEvent = ::XClientMessageEvent;
    using XMappingEvent = ::XMappingEvent;
    using XErrorEvent = ::XErrorEvent;
    using XAnyEvent = ::XAnyEvent;
    using XGenericEvent = ::XGenericEvent;
    using XGenericEventCookie = ::XGenericEventCookie;
    
    // X11 constants - use the actual macro values, not the names
    constexpr unsigned long X11_None = 0L;
    constexpr int X11_True = 1;
    constexpr int X11_False = 0;
    constexpr int X11_Success = 0;
    constexpr int X11_BadRequest = 1;
    constexpr int X11_BadValue = 2;
    constexpr int X11_BadWindow = 3;
    constexpr int X11_BadPixmap = 4;
    constexpr int X11_BadAtom = 5;
    constexpr int X11_BadCursor = 6;
    constexpr int X11_BadFont = 7;
    constexpr int X11_BadMatch = 8;
    constexpr int X11_BadDrawable = 9;
    constexpr int X11_BadAccess = 10;
    constexpr int X11_BadAlloc = 11;
    constexpr int X11_BadColor = 12;
    constexpr int X11_BadGC = 13;
    constexpr int X11_BadIDChoice = 14;
    constexpr int X11_BadName = 15;
    constexpr int X11_BadLength = 16;
    constexpr int X11_BadImplementation = 17;
    
    // Event types
    constexpr int X11_KeyPress = 2;
    constexpr int X11_KeyRelease = 3;
    constexpr int X11_ButtonPress = 4;
    constexpr int X11_ButtonRelease = 5;
    constexpr int X11_MotionNotify = 6;
    constexpr int X11_EnterNotify = 7;
    constexpr int X11_LeaveNotify = 8;
    constexpr int X11_FocusIn = 9;
    constexpr int X11_FocusOut = 10;
    constexpr int X11_KeymapNotify = 11;
    constexpr int X11_Expose = 12;
    constexpr int X11_GraphicsExpose = 13;
    constexpr int X11_NoExpose = 14;
    constexpr int X11_VisibilityNotify = 15;
    constexpr int X11_CreateNotify = 16;
    constexpr int X11_DestroyNotify = 17;
    constexpr int X11_UnmapNotify = 18;
    constexpr int X11_MapNotify = 19;
    constexpr int X11_MapRequest = 20;
    constexpr int X11_ReparentNotify = 21;
    constexpr int X11_ConfigureNotify = 22;
    constexpr int X11_ConfigureRequest = 23;
    constexpr int X11_GravityNotify = 24;
    constexpr int X11_ResizeRequest = 25;
    constexpr int X11_CirculateNotify = 26;
    constexpr int X11_CirculateRequest = 27;
    constexpr int X11_PropertyNotify = 28;
    constexpr int X11_SelectionClear = 29;
    constexpr int X11_SelectionRequest = 30;
    constexpr int X11_SelectionNotify = 31;
    constexpr int X11_ColormapNotify = 32;
    constexpr int X11_ClientMessage = 33;
    constexpr int X11_MappingNotify = 34;
    constexpr int X11_GenericEvent = 35;
    constexpr int X11_LASTEvent = 36;
    
    // Window classes
    constexpr int X11_InputOutput = 1;
    constexpr int X11_InputOnly = 2;
    
    // Common functions (optional - for convenience)
    inline Display* OpenDisplay(const char* display_name) {
        return XOpenDisplay(display_name);
    }
    
    inline int CloseDisplay(Display* display) {
        return XCloseDisplay(display);
    }
}

// NOW undefine X11 macros to protect Qt/LLVM
#ifdef None
#undef None
#endif
#ifdef True
#undef True
#endif
#ifdef False
#undef False
#endif
#ifdef Success
#undef Success
#endif
#ifdef Status
#undef Status
#endif
#ifdef Bool
#undef Bool
#endif