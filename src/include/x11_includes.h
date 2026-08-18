#pragma once
#include "havel_platform.h"

#if HAVEL_PLATFORM_LINUX && defined(HAVE_X11)
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/XKBlib.h>
#include <X11/extensions/XTest.h>
#include <X11/keysym.h>
#include <X11/XF86keysym.h>
#else
#ifndef None
#define None 0L
#endif
#ifndef True
#define True 1
#endif
#ifndef False
#define False 0
#endif
typedef unsigned long Window;
typedef unsigned long Atom;
typedef void *Display;
typedef unsigned long KeySym;
#endif
