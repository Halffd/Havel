#ifndef X11_INCLUDES_H
#define X11_INCLUDES_H

// X11 headers must be included in the correct order
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/Xatom.h>
#include <X11/X.h>
#include <X11/keysym.h>
#include <X11/XF86keysym.h>
#include <X11/extensions/XTest.h>
#include <X11/extensions/Xrandr.h>
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

#endif // X11_INCLUDES_H
