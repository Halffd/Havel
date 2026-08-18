#ifndef X11_INCLUDES_H
#define X11_INCLUDES_H

#include "havel_platform.h"

#if defined(HAVE_X11) && HAVEL_PLATFORM_LINUX

// X11 headers must be included in the correct order
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/Xatom.h>
#include <X11/X.h>
#include <X11/keysym.h>
#include <X11/XF86keysym.h>
#include <X11/extensions/XTest.h>
#ifdef HAVE_XRANDR
#include <X11/extensions/Xrandr.h>
#endif
#include <X11/extensions/shape.h>
#include <X11/extensions/Xfixes.h>
#include <X11/extensions/Xrender.h>

// Common X11 types and macros
#ifndef None
#define None 0L
#endif

#ifndef Bool
#define Bool int
#endif

#ifndef True
#define True 1
#endif

#ifndef False
#define False 0
#endif

// Shape extension defines if not already defined
#ifndef ShapeSet
#define ShapeSet 0
#endif

#ifndef ShapeInput
#define ShapeInput 2
#endif

// For XA_CARDINAL
#include <X11/Xatom.h>

#else // !HAVE_X11 or non-Linux

// Provide no-op stubs for the most common macros/typedefs so that
// files which forward-declare X11 types still parse on non-Linux builds.
#ifndef None
#define None 0L
#endif
#ifndef Bool
#define Bool int
#endif
#ifndef True
#define True 1
#endif
#ifndef False
#define False 0
#endif
#ifndef ShapeSet
#define ShapeSet 0
#endif
#ifndef ShapeInput
#define ShapeInput 2
#endif

typedef unsigned long Window;
typedef unsigned long Atom;
typedef unsigned long Pixmap;
typedef void *Display;
typedef struct _XEvent XEvent;
typedef unsigned long KeySym;
typedef unsigned long Time;
struct XVisualInfo;
struct XWindowAttributes;
struct XSetWindowAttributes;
union _XEvent;

#endif // HAVE_X11 && LINUX

#endif // X11_INCLUDES_H
