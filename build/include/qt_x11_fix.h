
#pragma once
// Auto-generated Qt+X11 compatibility header

#ifdef None
#undef None
#endif

#ifdef Bool
#undef Bool
#endif

#ifdef Status
#undef Status
#endif

#ifdef True
#undef True
#endif

#ifdef False
#undef False
#endif

// Include Qt headers first
#include <QObject>
#include <QWidget>
#include <QApplication>
#include <QMainWindow>
#include <QSystemTrayIcon>
#include <QTimer>
#include <QMenu>

// Then X11 headers
#include <X11/Xlib.h>

// Restore X11 macros for X11 code
#ifndef None
#define None 0L
#endif

#ifndef True
#define True 1
#endif

#ifndef False
#define False 0
#endif
