// Injects a synthetic key press into the X server with XTest.
//
// Used by the X11 hotkey delivery end-to-end check. The event is
// synthesized by the X server, so it travels the exact same path as a real
// key press: XInput -> root window event mask -> the client's key event
// queue. Nothing physical is involved.
//
// usage: x11inject <keysym> [ctrl]
//   keysym: an X keysym name, e.g. F24
//   ctrl:   "ctrl" to hold Control down for the press
//
// Prints "INJECTED" on stdout once the event has been sent.
#include <X11/Xlib.h>
#include <X11/keysym.h>
#include <X11/extensions/XTest.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int main(int argc, char **argv) {
  if (argc < 2) {
    fprintf(stderr, "usage: %s <keysym> [ctrl]\n", argv[0]);
    return 2;
  }

  const char *keysym_name = argv[1];
  const int with_ctrl = (argc >= 3 && strcmp(argv[2], "ctrl") == 0);

  Display *dpy = XOpenDisplay(NULL);
  if (!dpy) {
    fprintf(stderr, "XOpenDisplay failed\n");
    return 2;
  }

  KeySym sym = XStringToKeysym(keysym_name);
  if (sym == NoSymbol) {
    fprintf(stderr, "unknown keysym '%s'\n", keysym_name);
    XCloseDisplay(dpy);
    return 2;
  }
  KeyCode code = XKeysymToKeycode(dpy, sym);
  if (code == 0) {
    fprintf(stderr, "no keycode for keysym '%s'\n", keysym_name);
    XCloseDisplay(dpy);
    return 2;
  }

  if (with_ctrl) {
    // Hold the left Control key down for the duration of the press.
    KeyCode ctrl = XKeysymToKeycode(dpy, XK_Control_L);
    XTestFakeKeyEvent(dpy, ctrl, True, CurrentTime);
    XFlush(dpy);
    // Give the server a moment to register the modifier before the key.
    XSync(dpy, False);
  }

  XTestFakeKeyEvent(dpy, code, True, CurrentTime);
  XSync(dpy, False);
  XTestFakeKeyEvent(dpy, code, False, CurrentTime);
  XSync(dpy, False);

  if (with_ctrl) {
    KeyCode ctrl = XKeysymToKeycode(dpy, XK_Control_L);
    XTestFakeKeyEvent(dpy, ctrl, False, CurrentTime);
    XSync(dpy, False);
  }

  printf("INJECTED\n");
  fflush(stdout);
  XCloseDisplay(dpy);
  return 0;
}
