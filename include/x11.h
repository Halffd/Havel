#pragma once
// Safe X11 wrapper - use this instead of direct X11 includes
namespace x11 {
    // X11 constants - use the actual macro values, not the names
    using XStatus = int;
    using XBool = int;
    constexpr unsigned long XNone = 0L;
    constexpr int XTrue = 1;
    constexpr int XFalse = 0;
    constexpr int XSuccess = 0;
    constexpr int XBadRequest = 1;
    constexpr int XBadValue = 2;
    constexpr int XBadWindow = 3;
    constexpr int XBadPixmap = 4;
    constexpr int XBadAtom = 5;
    constexpr int XBadCursor = 6;
    constexpr int XBadFont = 7;
    constexpr int XBadMatch = 8;
    constexpr int XBadDrawable = 9;
    constexpr int XBadAccess = 10;
    constexpr int XBadAlloc = 11;
    constexpr int XBadColor = 12;
    constexpr int XBadGC = 13;
    constexpr int XBadIDChoice = 14;
    constexpr int XBadName = 15;
    constexpr int XBadLength = 16;
    constexpr int XBadImplementation = 17;
    
    // Event types
    constexpr int XKeyPress = 2;
    constexpr int XKeyRelease = 3;
    constexpr int XButtonPress = 4;
    constexpr int XButtonRelease = 5;
    constexpr int XMotionNotify = 6;
    constexpr int XEnterNotify = 7;
    constexpr int XLeaveNotify = 8;
    constexpr int XFocusIn = 9;
    constexpr int XFocusOut = 10;
    constexpr int XKeymapNotify = 11;
    constexpr int XExpose = 12;
    constexpr int XGraphicsExpose = 13;
    constexpr int XNoExpose = 14;
    constexpr int XVisibilityNotify = 15;
    constexpr int XCreateNotify = 16;
    constexpr int XDestroyNotify = 17;
    constexpr int XUnmapNotify = 18;
    constexpr int XMapNotify = 19;
    constexpr int XMapRequest = 20;
    constexpr int XReparentNotify = 21;
    constexpr int XConfigureNotify = 22;
    constexpr int XConfigureRequest = 23;
    constexpr int XGravityNotify = 24;
    constexpr int XResizeRequest = 25;
    constexpr int XCirculateNotify = 26;
    constexpr int XCirculateRequest = 27;
    constexpr int XPropertyNotify = 28;
    constexpr int XSelectionClear = 29;
    constexpr int XSelectionRequest = 30;
    constexpr int XSelectionNotify = 31;
    constexpr int XColormapNotify = 32;
    constexpr int XClientMessage = 33;
    constexpr int XMappingNotify = 34;
    constexpr int XGenericEventType = 35;  // Renamed to avoid collision
    constexpr int XLASTEvent = 36;
    
    // Window classes
    constexpr int XInputOutput = 1;
    constexpr int XInputOnly = 2;

    constexpr int XCurrentTime = 0L;
    constexpr int XNoSymbol = 0L;
    constexpr int XGrabModeSync = 0;
    constexpr int XGrabModeAsync = 1;
    constexpr int XRevertToNone = 0;
    constexpr int XRevertToPointerRoot = 1;
    constexpr int XRevertToParent = 2;
    
    // Grab status  
    constexpr int XGrabSuccess = 0;
    constexpr int XBadTime = 2;
    constexpr int XGrabInvalidTime = 2;
    constexpr int XGrabNotViewable = 3;
    constexpr int XGrabFrozen = 4;
    
    // Window attributes
    constexpr unsigned long XCWBackPixel = 1L<<1;
    constexpr unsigned long XCWBorderPixel = 1L<<3;
    constexpr unsigned long XCWEventMask = 1L<<11;
    constexpr unsigned long XCWOverrideRedirect = 1L<<9;
    
    // Event masks
    constexpr long XKeyPressMask = 1L<<0;
    constexpr long XKeyReleaseMask = 1L<<1;
    constexpr long XButtonPressMask = 1L<<2;
    constexpr long XButtonReleaseMask = 1L<<3;
    constexpr long XPointerMotionMask = 1L<<6;
    constexpr long XStructureNotifyMask = 1L<<17;
}

// Include X11 headers 
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/extensions/XTest.h>
#include <X11/extensions/Xrandr.h>
#include <X11/extensions/XInput2.h>
#include <X11/extensions/Xfixes.h>
#include <X11/extensions/Xge.h>
#include <X11/keysym.h>
#include <X11/XKBlib.h>
#include <X11/XF86keysym.h>
#include <X11/Xatom.h>

// IMMEDIATELY kill ALL X11 macros
#undef None
#undef True
#undef False
#undef Success
#undef Status
#undef Bool
#undef Always
#undef DestroyAll
#undef Absolute
#undef BadRequest
#undef BadValue
#undef BadWindow
#undef BadPixmap
#undef BadAtom
#undef BadCursor
#undef BadFont
#undef BadMatch
#undef BadDrawable
#undef BadAccess
#undef BadAlloc
#undef BadColor
#undef BadGC
#undef BadIDChoice
#undef BadName
#undef BadLength
#undef BadImplementation
#undef KeyPress
#undef KeyRelease
#undef ButtonPress
#undef ButtonRelease
#undef MotionNotify
#undef EnterNotify
#undef LeaveNotify
#undef FocusIn
#undef FocusOut
#undef KeymapNotify
#undef Expose
#undef GraphicsExpose
#undef NoExpose
#undef VisibilityNotify
#undef CreateNotify
#undef DestroyNotify
#undef UnmapNotify
#undef MapNotify
#undef MapRequest
#undef ReparentNotify
#undef ConfigureNotify
#undef ConfigureRequest
#undef GravityNotify
#undef ResizeRequest
#undef CirculateNotify
#undef CirculateRequest
#undef PropertyNotify
#undef SelectionClear
#undef SelectionRequest
#undef SelectionNotify
#undef ColormapNotify
#undef ClientMessage
#undef MappingNotify
#undef GenericEvent
#undef LASTEvent
#undef InputOutput
#undef InputOnly
#ifdef CursorShape
#undef CursorShape
#endif
#ifdef MCStreamer
#undef MCStreamer
#endif

// X11 types in safe namespace
namespace x11 {
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
    using XGenericEvent = ::XGenericEvent;  // Now safe - no collision
    using XGenericEventCookie = ::XGenericEventCookie;
    
    // Function wrappers
    inline Display* OpenDisplay(const char* display_name) {
        return XOpenDisplay(display_name);
    }
    
    inline int CloseDisplay(Display* display) {
        return XCloseDisplay(display);
    }
}