#include "core/IO.hpp"
#include "core/ConfigManager.hpp"
#include "core/DisplayManager.hpp"
#include "utils/Logger.hpp"
#include "utils/Util.hpp"
#include "utils/Utils.hpp"
#include "x11.h"
#include <chrono>
#include <cstring>
#include <fcntl.h>
#include <linux/input.h>
#include <linux/uinput.h>
#include <unistd.h>

// X11 includes
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/extensions/XI2proto.h>
#include <X11/extensions/XInput.h>
#include <X11/extensions/XInput2.h>
#include <X11/extensions/XTest.h>
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <iostream>
#include <regex>
#include <string>
#include <thread>
#include <vector>

namespace havel {
#if defined(WINDOWS)
HHOOK IO::keyboardHook = NULL;
#endif
std::unordered_map<int, HotKey> IO::hotkeys; // Map to store hotkeys by ID
bool IO::hotkeyEnabled = true;
int IO::hotkeyCount = 0;
int IO::XErrorHandler(Display *dpy, XErrorEvent *ee) {
  if (ee->error_code == x11::XBadWindow ||
      (ee->request_code == X_GrabButton && ee->error_code == x11::XBadAccess) ||
      (ee->request_code == X_GrabKey && ee->error_code == x11::XBadAccess)) {
    return 0;
  }
  error("X11 error: request_code={}, error_code={}", ee->request_code,
        ee->error_code);
  return 0; // Don't crash
}
IO::IO() {
  info("IO constructor called");

  // Set the error handler before making your XGrabKey call
  XSetErrorHandler(IO::XErrorHandler);
  DisplayManager::Initialize();
  display = DisplayManager::GetDisplay();

  // Initialize XInput2 if available
  xinput2Available = InitializeXInput2();
  if (xinput2Available) {
    info("XInput2 initialized successfully");
  } else {
    warning(
        "XInput2 initialization failed, falling back to software sensitivity");
  }

  // Initialize mouse event timestamps
  lastLeftPress = std::chrono::steady_clock::now();
  lastRightPress = std::chrono::steady_clock::now();
  InitKeyMap();

  // Start hotkey monitoring thread for X11
#ifdef __linux__
  if (display) {
    UpdateNumLockMask();

    // Initialize keyboard device
    std::string keyboardDevice = getKeyboardDevice();
    if (!keyboardDevice.empty()) {
      try {
        info("Using keyboard device: {}", keyboardDevice);
        StartEvdevHotkeyListener(keyboardDevice);
        info("Successfully started evdev hotkey listener for keyboard");
      } catch (const std::exception &e) {
        error("Failed to start evdev keyboard listener: {}", e.what());
        globalEvdev = false;
      }
    } else {
      globalEvdev = false;
      error("Failed to find a suitable keyboard device");
    }

    // Initialize mouse device if needed
    std::string mouseDevice = getMouseDevice();
    if (!mouseDevice.empty() && mouseDevice != keyboardDevice) {
      try {
        info("Using mouse device: {}", mouseDevice);
        StartEvdevMouseListener(mouseDevice);
        info("Successfully started evdev mouse listener");
      } catch (const std::exception &e) {
        error("Failed to start evdev mouse listener: {}", e.what());
      }
    } else if (mouseDevice.empty()) {
      warning("No suitable mouse device found");
    }

    // Fall back to X11 hotkeys if evdev initialization failed
    if (!globalEvdev || !x11Hotkeys.empty()) {
      timerRunning = true;
      try {
        timerThread = std::thread(&IO::MonitorHotkeys, this);
        info("Started X11 hotkey monitoring thread");
      } catch (const std::exception &e) {
        error("Failed to start X11 hotkey monitoring thread: {}", e.what());
        timerRunning = false;
      }
    }
  }
#endif
}
IO::~IO() {
  std::cout << "IO destructor called" << std::endl;

  // Stop the hotkey monitoring thread
  if (timerRunning && timerThread.joinable()) {
    timerRunning = false;
    timerThread.join();
  }

  // Stop the evdev listener if it's running
  StopEvdevHotkeyListener();

  // Ungrab all hotkeys before closing
#ifdef __linux__
  if (display) {
    Window root = DefaultRootWindow(display);

    // First, ungrab all hotkeys from the instance
    for (const auto &[id, hotkey] : instanceHotkeys) {
      if (hotkey.key != 0) {
        KeyCode keycode = XKeysymToKeycode(display, hotkey.key);
        if (keycode != 0) {
          Ungrab(keycode, hotkey.modifiers, root);
        }
      }
    }

    // Then, ungrab all static hotkeys
    for (const auto &[id, hotkey] : hotkeys) {
      if (hotkey.key != 0) {
        KeyCode keycode = XKeysymToKeycode(display, hotkey.key);
        if (keycode != 0) {
          Ungrab(keycode, hotkey.modifiers, root);
        }
      }
    }

    // Sync to ensure all ungrabs are processed
    XSync(display, x11::XFalse);
  }

  // Clean up uinput device
  CleanupUinputDevice();

  // Close the uinput file descriptor if it's open
  if (uinputFd >= 0) {
    close(uinputFd);
    uinputFd = -1;
  }
#endif

  // Don't close the display here, it's managed by DisplayManager
  display = nullptr;

  std::cout << "IO cleanup completed" << std::endl;
}

bool IO::SetupUinputDevice() {
  uinputFd = open("/dev/uinput", O_WRONLY | O_NONBLOCK);
  if (uinputFd < 0) {
    std::cerr << "uinput: failed to open /dev/uinput: " << strerror(errno)
              << "\n";
    return false;
  }

  struct uinput_setup usetup = {};
  usetup.id.bustype = BUS_USB;
  usetup.id.vendor = 0x1234;
  usetup.id.product = 0x5678;
  strcpy(usetup.name, "havel-uinput-kb");
  // Enable key event support
  if (ioctl(uinputFd, UI_SET_EVBIT, EV_KEY) < 0)
    goto error;
  if (ioctl(uinputFd, UI_SET_EVBIT, EV_SYN) < 0)
    goto error;
  ioctl(uinputFd, UI_SET_KEYBIT, BTN_LEFT);
  ioctl(uinputFd, UI_SET_KEYBIT, BTN_MIDDLE);
  ioctl(uinputFd, UI_SET_KEYBIT, BTN_RIGHT);
  ioctl(uinputFd, UI_SET_KEYBIT, BTN_SIDE);
  ioctl(uinputFd, UI_SET_KEYBIT, BTN_EXTRA);
  ioctl(uinputFd, UI_SET_EVBIT, EV_REL);
  ioctl(uinputFd, UI_SET_RELBIT, REL_WHEEL);
  ioctl(uinputFd, UI_SET_RELBIT, REL_HWHEEL);

  // Enable all possible key codes you might emit
  for (int i = 0; i < 256; ++i)
    ioctl(uinputFd, UI_SET_KEYBIT, i);

  if (ioctl(uinputFd, UI_DEV_SETUP, &usetup) < 0)
    goto error;
  if (ioctl(uinputFd, UI_DEV_CREATE) < 0)
    goto error;

  std::this_thread::sleep_for(std::chrono::milliseconds(100));
  // Allow it to init
  return true;

error:
  std::cerr << "uinput: device setup failed: " << strerror(errno) << "\n";
  close(uinputFd);
  uinputFd = -1;
  return false;
}
bool IO::GrabKeyboard() {
  if (!display)
    return false;

  struct timespec ts = {.tv_sec = 0, .tv_nsec = 1000000}; // 1ms

  // Try to grab keyboard, may need to wait for other processes to ungrab
  for (int i = 0; i < 1000; i++) {
    int result = XGrabKeyboard(display, DefaultRootWindow(display), x11::XTrue,
                               GrabModeAsync, GrabModeAsync, CurrentTime);
    if (result == x11::XSuccess) {
      info("Successfully grabbed entire keyboard after {} attempts", i + 1);
      return true;
    }

    nanosleep(&ts, nullptr);
  }

  error("Cannot grab keyboard after 1000 attempts");
  return false;
}
bool IO::Grab(Key input, unsigned int modifiers, Window root, bool grab,
              bool isMouse) {
  if (!display)
    return false;

  bool success = true;
  bool isButton = isMouse || (input >= Button1 && input <= 7);

  unsigned int modVariants[] = {0, LockMask, numlockmask,
                                numlockmask | LockMask};

  // Single loop - just grab the 4 variants of THIS key
  for (unsigned int variant : modVariants) {
    unsigned int finalMods = modifiers | variant;
    int result;

    if (isButton) {
      result = XGrabButton(display, input, finalMods, root, x11::XTrue,
                           ButtonPressMask | ButtonReleaseMask, GrabModeAsync,
                           GrabModeAsync, x11::XNone, x11::XNone);
    } else {
      result = XGrabKey(display, input, finalMods, root, x11::XTrue,
                        GrabModeAsync, GrabModeAsync);
    }

    if (result != x11::XSuccess) {
      success = false;
    }
  }

  XSync(display, x11::XFalse);
  return success;
}

bool IO::GrabAllHotkeys() {
  if (!display)
    return false;

  UpdateNumLockMask(); // Once at start

  unsigned int modVariants[] = {0, LockMask, numlockmask,
                                numlockmask | LockMask};

  bool success = true;

  // Single pass through your configured hotkeys
  for (const auto &[id, hotkey] : hotkeys) {
    if (hotkey.evdev || !hotkey.grab)
      continue; // Skip evdev hotkeys

    // Grab 4 variants of each configured hotkey
    for (unsigned int variant : modVariants) {
      unsigned int finalMods = hotkey.modifiers | variant;

      int result =
          XGrabKey(display, hotkey.key, finalMods, DefaultRootWindow(display),
                   x11::XTrue, GrabModeAsync, GrabModeAsync);
      if (result != x11::XSuccess) {
        success = false;
      }
    }
  }

  XSync(display, x11::XFalse);
  return success;
}

void IO::Ungrab(Key input, unsigned int modifiers, Window root, bool isMouse) {
  if (!display)
    return;

  bool isButton = isMouse || (input >= Button1 && input <= 7);

  unsigned int modVariants[] = {0, LockMask, numlockmask,
                                numlockmask | LockMask};

  for (unsigned int variant : modVariants) {
    unsigned int finalMods = modifiers | variant;

    if (isButton) {
      XUngrabButton(display, input, finalMods, root);
    } else {
      XUngrabKey(display, input, finalMods, root);
    }
  }

  XSync(display, x11::XFalse);
}

void IO::UngrabAll() {
  if (!display)
    return;
  XUngrabKey(display, AnyKey, AnyModifier, DefaultRootWindow(display));
  XUngrabButton(display, AnyButton, AnyModifier, DefaultRootWindow(display));
  XSync(display, x11::XFalse);
}

// Fast version - no retries
bool IO::FastGrab(Key input, unsigned int modifiers, Window root) {
  if (!display)
    return false;

  unsigned int variants[] = {modifiers, modifiers | LockMask,
                             modifiers | numlockmask,
                             modifiers | numlockmask | LockMask};

  // Just try once, no retries
  for (unsigned int mods : variants) {
    XGrabKey(display, input, mods, root, x11::XTrue, GrabModeAsync,
             GrabModeAsync);
  }

  return true;
}
bool IO::ModifierMatch(unsigned int expected, unsigned int actual) {
  return CLEANMASK(expected) == CLEANMASK(actual);
}
void IO::MonitorHotkeys() {
#ifdef __linux__
  info("Starting X11 hotkey monitoring thread");

  // Lock scope for initialization
  {
    std::lock_guard<std::mutex> lock(x11Mutex);
    if (!display) {
      error("Display is null, cannot monitor hotkeys");
      return;
    }
  }

  if (!XInitThreads()) {
    error("Failed to initialize X11 threading support");
    return;
  }

  XEvent event;
  Window root;

  // Initialize root window with lock
  {
    std::lock_guard<std::mutex> lock(x11Mutex);
    root = DefaultRootWindow(display);
    XSelectInput(display, root, KeyPressMask | KeyReleaseMask);
  }

  // Helper function to check if a keysym is a modifier
  constexpr auto IsModifierKey = [](KeySym ks) -> bool {
    return ks == XK_Shift_L || ks == XK_Shift_R || ks == XK_Control_L ||
           ks == XK_Control_R || ks == XK_Alt_L || ks == XK_Alt_R ||
           ks == XK_Meta_L || ks == XK_Meta_R || ks == XK_Super_L ||
           ks == XK_Super_R || ks == XK_Hyper_L || ks == XK_Hyper_R ||
           ks == XK_Caps_Lock || ks == XK_Shift_Lock || ks == XK_Num_Lock ||
           ks == XK_Scroll_Lock;
  };

  // Pre-allocate containers
  std::vector<std::function<void()>> callbacks;
  callbacks.reserve(16);

  // Error message constants
  static const std::string callback_error_prefix = "Error in hotkey callback: ";
  static const std::string event_error_prefix = "Error processing X11 event: ";

  constexpr unsigned int relevantModifiers =
      ShiftMask | LockMask | ControlMask | Mod1Mask | Mod4Mask | Mod5Mask;

  try {
    while (timerRunning) {
      int pendingEvents = 0;

      // Check for events with minimal lock
      {
        std::lock_guard<std::mutex> lock(x11Mutex);
        if (!display) {
          error("Display connection lost");
          break;
        }
        pendingEvents = XPending(display);
      }

      if (pendingEvents == 0) {
        // No events, sleep briefly to avoid busy waiting
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
        continue;
      }

      // Process all pending events in batch
      for (int i = 0; i < pendingEvents && timerRunning; ++i) {
        // Get next event with lock
        {
          std::lock_guard<std::mutex> lock(x11Mutex);
          if (!display || XNextEvent(display, &event) != 0) {
            error("XNextEvent failed - X11 connection error");
            timerRunning = false;
            break;
          }
        }

        try {
          // Process event without lock (no X11 calls here)
          if (event.type != x11::XKeyPress && event.type != x11::XKeyRelease) {
            continue;
          }

          const bool isDown = (event.type == x11::XKeyPress);
          const XKeyEvent *keyEvent = &event.xkey;

          KeySym keysym;
          {
            std::lock_guard<std::mutex> lock(x11Mutex);
            if (!display)
              break;
            keysym = XLookupKeysym(const_cast<XKeyEvent *>(keyEvent), 0);
          }

          if (keysym == NoSymbol) {
            continue;
          }

          // Handle remapped keys
          if (const auto remappedIt = remappedKeys.find(keysym);
              remappedIt != remappedKeys.end() &&
              remappedIt->second != NoSymbol) {

            const char *keySymStr;
            {
              std::lock_guard<std::mutex> lock(x11Mutex);
              if (!display)
                break;
              keySymStr = XKeysymToString(remappedIt->second);
            }

            if (keySymStr) {
              if (const int keycode = EvdevNameToKeyCode(keySymStr);
                  keycode != -1) {
                SendUInput(keycode, isDown);
              }
            }
            continue; // Suppress original event
          }

          // Handle mapped keys
          if (const auto mappedIt = keyMapInternal.find(keysym);
              mappedIt != keyMapInternal.end()) {

            const char *keySymStr;
            {
              std::lock_guard<std::mutex> lock(x11Mutex);
              if (!display)
                break;
              keySymStr = XKeysymToString(mappedIt->second);
            }

            if (keySymStr) {
              if (const int keycode = EvdevNameToKeyCode(keySymStr);
                  keycode != -1) {
                SendUInput(keycode, isDown);
              }
            }
            continue; // Suppress original event
          }

          // Skip modifier keys
          if (IsModifierKey(keysym)) {
            continue;
          }

// Optional debug logging
#ifdef DEBUG_HOTKEYS
          {
            std::lock_guard<std::mutex> lock(x11Mutex);
            if (display) {
              if (const char *keysym_str = XKeysymToString(keysym)) {
                debug(std::string(isDown ? "KeyPress" : "KeyRelease") +
                      " event: " + keysym_str +
                      " (keycode: " + std::to_string(keyEvent->keycode) +
                      ", state: " + std::to_string(keyEvent->state) + ")");
              }
            }
          }
#endif

          const unsigned int cleanedState = keyEvent->state & relevantModifiers;
          callbacks.clear(); // Clear previous callbacks (no deallocation)

          // Find matching hotkeys
          {
            std::scoped_lock<std::mutex> lock(hotkeyMutex);
            for (const auto &[id, hotkey] : hotkeys) {
              if (!hotkey.enabled)
                continue;

              if (hotkey.key == keyEvent->keycode &&
                  static_cast<int>(cleanedState) == hotkey.modifiers) {

                // Check event type
                if ((hotkey.eventType == HotkeyEventType::Down && !isDown) ||
                    (hotkey.eventType == HotkeyEventType::Up && isDown)) {
                  continue;
                }

#ifdef DEBUG_HOTKEYS
                debug("Hotkey matched: " + hotkey.alias +
                      " (state: " + std::to_string(cleanedState) +
                      " vs expected: " + std::to_string(hotkey.modifiers) +
                      ")");
#endif

                if (hotkey.callback) {
                  callbacks.emplace_back(hotkey.callback);
                }
                break; // Only one hotkey should match per event
              }
            }
          }

          // Execute callbacks outside all locks
          for (const auto &callback : callbacks) {
            try {
              callback();
            } catch (const std::exception &e) {
              error(callback_error_prefix + e.what());
            } catch (...) {
              error("Unknown error in hotkey callback");
            }
          }

        } catch (const std::exception &e) {
          error(event_error_prefix + e.what());
        } catch (...) {
          error("Unknown error processing X11 event");
        }
      }
    }
  } catch (const std::exception &e) {
    error(std::string("Fatal error in hotkey monitoring: ") + e.what());
    timerRunning = false;
  } catch (...) {
    error("Unknown fatal error in hotkey monitoring");
    timerRunning = false;
  }

  info("Hotkey monitoring thread stopped");
#endif // __linux__
}
// X11 hotkey monitoring thread
void IO::InitKeyMap() {
  // Basic implementation of key map
  std::cout << "Initializing key map" << std::endl;

  // Initialize key mappings for common keys
#ifdef __linux__
  keyMap["esc"] = XK_Escape;
  keyMap["enter"] = XK_Return;
  keyMap["space"] = XK_space;
  keyMap["tab"] = XK_Tab;
  keyMap["backspace"] = XK_BackSpace;
  keyMap["ctrl"] = XK_Control_L;
  keyMap["alt"] = XK_Alt_L;
  keyMap["shift"] = XK_Shift_L;
  keyMap["win"] = XK_Super_L;
  keyMap["lwin"] = XK_Super_L; // Add alias for Left Win
  keyMap["rwin"] = XK_Super_R; // Add Right Win
  keyMap["up"] = XK_Up;
  keyMap["down"] = XK_Down;
  keyMap["left"] = XK_Left;
  keyMap["right"] = XK_Right;
  keyMap["delete"] = XK_Delete;
  keyMap["insert"] = XK_Insert;
  keyMap["home"] = XK_Home;
  keyMap["end"] = XK_End;
  keyMap["pageup"] = XK_Page_Up;
  keyMap["pagedown"] = XK_Page_Down;
  keyMap["printscreen"] = XK_Print;
  keyMap["scrolllock"] = XK_Scroll_Lock;
  keyMap["pause"] = XK_Pause;
  keyMap["capslock"] = XK_Caps_Lock;
  keyMap["numlock"] = XK_Num_Lock;
  keyMap["menu"] = XK_Menu; // Add Menu key

  // Numpad keys
  keyMap["kp_0"] = XK_KP_0;
  keyMap["kp_1"] = XK_KP_1;
  keyMap["kp_2"] = XK_KP_2;
  keyMap["kp_3"] = XK_KP_3;
  keyMap["kp_4"] = XK_KP_4;
  keyMap["kp_5"] = XK_KP_5;
  keyMap["kp_6"] = XK_KP_6;
  keyMap["kp_7"] = XK_KP_7;
  keyMap["kp_8"] = XK_KP_8;
  keyMap["kp_9"] = XK_KP_9;
  keyMap["kp_insert"] = XK_KP_Insert;      // KP 0
  keyMap["kp_end"] = XK_KP_End;            // KP 1
  keyMap["kp_down"] = XK_KP_Down;          // KP 2
  keyMap["kp_pagedown"] = XK_KP_Page_Down; // KP 3
  keyMap["kp_left"] = XK_KP_Left;          // KP 4
  keyMap["kp_begin"] = XK_KP_Begin;        // KP 5
  keyMap["kp_right"] = XK_KP_Right;        // KP 6
  keyMap["kp_home"] = XK_KP_Home;          // KP 7
  keyMap["kp_up"] = XK_KP_Up;              // KP 8
  keyMap["kp_pageup"] = XK_KP_Page_Up;     // KP 9
  keyMap["kp_delete"] = XK_KP_Delete;      // KP Decimal
  keyMap["kp_decimal"] = XK_KP_Decimal;
  keyMap["kp_add"] = XK_KP_Add;
  keyMap["kp_subtract"] = XK_KP_Subtract;
  keyMap["kp_multiply"] = XK_KP_Multiply;
  keyMap["kp_divide"] = XK_KP_Divide;
  keyMap["kp_enter"] = XK_KP_Enter;

  // Function keys
  keyMap["f1"] = XK_F1;
  keyMap["f2"] = XK_F2;
  keyMap["f3"] = XK_F3;
  keyMap["f4"] = XK_F4;
  keyMap["f5"] = XK_F5;
  keyMap["f6"] = XK_F6;
  keyMap["f7"] = XK_F7;
  keyMap["f8"] = XK_F8;
  keyMap["f9"] = XK_F9;
  keyMap["f10"] = XK_F10;
  keyMap["f11"] = XK_F11;
  keyMap["f12"] = XK_F12;

  // Media keys (using XF86keysym.h - ensure it's included)
  keyMap["volumeup"] = XF86XK_AudioRaiseVolume;
  keyMap["volumedown"] = XF86XK_AudioLowerVolume;
  keyMap["mute"] = XF86XK_AudioMute;
  keyMap["play"] = XF86XK_AudioPlay;
  keyMap["pause"] = XF86XK_AudioPause;
  keyMap["playpause"] = XF86XK_AudioPlay; // Often mapped to the same key
  keyMap["stop"] = XF86XK_AudioStop;
  keyMap["prev"] = XF86XK_AudioPrev;
  keyMap["next"] = XF86XK_AudioNext;

  // Punctuation and symbols
  keyMap["comma"] = XK_comma; // Add comma
  keyMap["period"] = XK_period;
  keyMap["semicolon"] = XK_semicolon;
  keyMap["slash"] = XK_slash;
  keyMap["backslash"] = XK_backslash;
  keyMap["bracketleft"] = XK_bracketleft;
  keyMap["bracketright"] = XK_bracketright;
  keyMap["minus"] = XK_minus; // Add minus
  keyMap["equal"] = XK_equal; // Add equal
  keyMap["grave"] = XK_grave; // Tilde key (~)
  keyMap["apostrophe"] = XK_apostrophe;

  // Letter keys (a-z)
  for (char c = 'a'; c <= 'z'; ++c) {
    keyMap[std::string(1, c)] = XStringToKeysym(std::string(1, c).c_str());
  }

  // Number keys (0-9)
  for (char c = '0'; c <= '9'; ++c) {
    keyMap[std::string(1, c)] = XStringToKeysym(std::string(1, c).c_str());
  }

  // Button names (for mouse events)
  keyMap["button1"] = Button1;
  keyMap["button2"] = Button2;
  keyMap["button3"] = Button3;
  keyMap["button4"] = Button4; // Wheel up
  keyMap["button5"] = Button5; // Wheel down

#endif
}

void IO::removeSpecialCharacters(str &keyName) {
  // Define the characters to remove
  const std::string charsToRemove = "^+!#*&";

  // Remove characters
  keyName.erase(std::remove_if(keyName.begin(), keyName.end(),
                               [&charsToRemove](char c) {
                                 return charsToRemove.find(c) !=
                                        std::string::npos;
                               }),
                keyName.end());
}
bool IO::EmitClick(int btnCode, int action) {
  static std::mutex uinputMutex;
  std::lock_guard<std::mutex> lock(uinputMutex);
  input_event ev = {};

  // action values:
  // 0 = Release
  // 1 = Hold
  // 2 = Click (press and release)

  if (action == 1) { // Hold
    ev.type = EV_KEY;
    ev.code = btnCode;
    ev.value = 1; // Press
    if (write(uinputFd, &ev, sizeof(ev)) < 0)
      return false;

    ev.type = EV_SYN;
    ev.code = SYN_REPORT;
    ev.value = 0;
    if (write(uinputFd, &ev, sizeof(ev)) < 0)
      return false;

    return true;
  } else if (action == 0) { // Release
    ev.type = EV_KEY;
    ev.code = btnCode;
    ev.value = 0; // Release
    if (write(uinputFd, &ev, sizeof(ev)) < 0)
      return false;

    ev.type = EV_SYN;
    ev.code = SYN_REPORT;
    ev.value = 0;
    if (write(uinputFd, &ev, sizeof(ev)) < 0)
      return false;

    return true;
  } else if (action == 2) { // Click (press and release)
    // Press
    ev.type = EV_KEY;
    ev.code = btnCode;
    ev.value = 1;
    if (write(uinputFd, &ev, sizeof(ev)) < 0)
      return false;

    // Sync
    ev.type = EV_SYN;
    ev.code = SYN_REPORT;
    ev.value = 0;
    if (write(uinputFd, &ev, sizeof(ev)) < 0)
      return false;

    // Small delay to make it a click
    std::this_thread::sleep_for(std::chrono::milliseconds(20));

    // Release
    ev.type = EV_KEY;
    ev.code = btnCode;
    ev.value = 0;
    if (write(uinputFd, &ev, sizeof(ev)) < 0)
      return false;
    // Sync
    ev.type = EV_SYN;
    ev.code = SYN_REPORT;
    ev.value = 0;
    if (write(uinputFd, &ev, sizeof(ev)) < 0)
      return false;
  } else if (action == 1 || action == 0) {
    ev.type = EV_KEY;
    ev.code = btnCode;
    ev.value = action;
    if (write(uinputFd, &ev, sizeof(ev)) < 0)
      return false;
    // Sync
    ev.type = EV_SYN;
    ev.code = SYN_REPORT;
    ev.value = 0;
    if (write(uinputFd, &ev, sizeof(ev)) < 0)
      return false;
  } else {
    std::cerr << "Invalid mouse action: " << action << "\n";
    return false;
  }

  return true;
}
bool IO::MouseMove(int dx, int dy, int speed, float accel) {
  // Apply mouse sensitivity
  double adjustedDx, adjustedDy;
  {
    std::lock_guard<std::mutex> lock(mouseMutex);
    adjustedDx = dx * mouseSensitivity;
    adjustedDy = dy * mouseSensitivity;
  }

  static std::mutex uinputMutex;
  std::lock_guard<std::mutex> ioLock(uinputMutex);

  dx = static_cast<int>(adjustedDx);
  dy = static_cast<int>(adjustedDy);

  if (speed <= 0)
    speed = 1;
  if (accel <= 0.0f)
    accel = 1.0f;

  // Apply acceleration curve (exponential for more natural feeling)
  float acceleratedSpeed = speed * std::pow(accel, 1.5f);

  // Calculate movement with sub-pixel precision but integer output
  int actualDx = static_cast<int>(dx * acceleratedSpeed);
  int actualDy = static_cast<int>(dy * acceleratedSpeed);

  // For very small movements, ensure at least 1 pixel movement
  if (actualDx == 0 && dx != 0)
    actualDx = (dx > 0) ? 1 : -1;
  if (actualDy == 0 && dy != 0)
    actualDy = (dy > 0) ? 1 : -1;

  // Send the events
  input_event ev = {};

  if (actualDx != 0) {
    ev.type = EV_REL;
    ev.code = REL_X;
    ev.value = actualDx;
    if (write(uinputFd, &ev, sizeof(ev)) < 0)
      return false;
  }

  if (actualDy != 0) {
    ev.type = EV_REL;
    ev.code = REL_Y;
    ev.value = actualDy;
    if (write(uinputFd, &ev, sizeof(ev)) < 0)
      return false;
  }

  // Sync event
  ev.type = EV_SYN;
  ev.code = SYN_REPORT;
  ev.value = 0;
  if (write(uinputFd, &ev, sizeof(ev)) < 0)
    return false;

  return true;
}
// Set mouse sensitivity (0.1 - 10.0)
void IO::SetMouseSensitivity(double sensitivity) {
  std::lock_guard<std::mutex> lock(mouseMutex);
  // Clamp sensitivity between 0.1 and 10.0
  sensitivity = std::max(0.1, std::min(10.0, sensitivity));

  // Try to set hardware sensitivity first
  bool hardwareSuccess = false;
  if (xinput2Available) {
    hardwareSuccess = SetHardwareMouseSensitivity(sensitivity);
  }

  // Fall back to software sensitivity if hardware control fails
  if (!hardwareSuccess) {
    mouseSensitivity = sensitivity;
    info("Using software mouse sensitivity: {}", mouseSensitivity);
  } else {
    // Keep them in sync in case we need to fall back later
    mouseSensitivity = sensitivity;
  }
}

bool IO::InitializeXInput2() {
  if (!display) {
    error("Cannot initialize XInput2: No display");
    return false;
  }

  int xi_opcode, event, xi_error;
  if (!XQueryExtension(display, "XInputExtension", &xi_opcode, &event,
                       &xi_error)) {
    error("X Input extension not available");
    return false;
  }

  // Check XInput2 version
  int major = 2, minor = 0;
  if (XIQueryVersion(display, &major, &minor) != x11::XSuccess) {
    error("XInput2 not supported by server");
    return false;
  }

  // Find the first pointer device that supports XInput2
  XIDeviceInfo *devices;
  int ndevices;
  devices = XIQueryDevice(display, XIAllDevices, &ndevices);

  for (int i = 0; i < ndevices; i++) {
    XIDeviceInfo *dev = &devices[i];
    if (dev->use == XISlavePointer || dev->use == XIFloatingSlave) {
      xinput2DeviceId = dev->deviceid;
      break;
    }
  }

  XIFreeDeviceInfo(devices);

  if (xinput2DeviceId == -1) {
    error("No suitable XInput2 pointer device found");
    return false;
  }

  return true;
}

bool IO::SetHardwareMouseSensitivity(double sensitivity) {
  if (!display || xinput2DeviceId == -1) {
    return false;
  }

  // Clamp sensitivity to a reasonable range for hardware
  sensitivity = std::max(0.1, std::min(10.0, sensitivity));

  // Convert sensitivity to X's acceleration (sensitivity^2 for better curve)
  double accel_numerator = sensitivity * sensitivity * 10.0;
  double accel_denominator = 10.0;
  double threshold = 0.0; // No threshold for continuous acceleration

  // Set the device's acceleration
  XDevice *dev = XOpenDevice(display, xinput2DeviceId);
  if (!dev) {
    error("Failed to open XInput2 device");
    return false;
  }

  XDeviceControl *control =
      (XDeviceControl *)XGetDeviceControl(display, dev, DEVICE_RESOLUTION);
  if (!control) {
    XCloseDevice(display, dev);
    return false;
  }

  XChangePointerControl(display, x11::XTrue, x11::XTrue,
                        (int)(accel_numerator * 10),
                        (int)(accel_denominator * 10), (int)threshold);

  XFree(control);
  XCloseDevice(display, dev);

  info("Set hardware mouse sensitivity to: {}", sensitivity);
  return true;
}

// Get current mouse sensitivity
double IO::GetMouseSensitivity() const {
  std::lock_guard<std::mutex> lock(mouseMutex);
  return mouseSensitivity;
}

// Set scroll speed (0.1 - 10.0)
void IO::SetScrollSpeed(double speed) {
  std::lock_guard<std::mutex> lock(mouseMutex);
  // Clamp scroll speed between 0.1 and 10.0
  scrollSpeed = std::max(0.1, std::min(10.0, speed));
  info("Scroll speed set to: {}", scrollSpeed);
}

// Get current scroll speed
double IO::GetScrollSpeed() const {
  std::lock_guard<std::mutex> lock(mouseMutex);
  return scrollSpeed;
}

// Enhanced mouse movement with custom sensitivity
bool IO::MouseMoveSensitive(int dx, int dy, int baseSpeed, float accel) {
  std::lock_guard<std::mutex> lock(mouseMutex);

  // Apply sensitivity and acceleration
  double adjustedDx = dx * mouseSensitivity;
  double adjustedDy = dy * mouseSensitivity;

  // Apply acceleration if enabled (accel > 1.0)
  if (accel > 1.0f) {
    double distance = std::sqrt(dx * dx + dy * dy);
    if (distance > 1.0) {
      double factor = 1.0 + (accel - 1.0) * (distance / 100.0);
      adjustedDx *= factor;
      adjustedDy *= factor;
    }
  }

  // Call the original MouseMove with adjusted values
  return MouseMove(static_cast<int>(adjustedDx), static_cast<int>(adjustedDy),
                   baseSpeed, 1.0f);
}

bool IO::Scroll(int dy, int dx) {
  std::lock_guard<std::mutex> lock(mouseMutex);

  // Apply scroll speed
  if (dy != 0)
    dy = static_cast<int>(dy * scrollSpeed);
  if (dx != 0)
    dx = static_cast<int>(dx * scrollSpeed);

  if (uinputFd < 0)
    return false;

  input_event ev = {};

  auto emit = [&](uint16_t code, int value) -> bool {
    ev.type = EV_REL;
    ev.code = code;
    ev.value = value;
    if (write(uinputFd, &ev, sizeof(ev)) < 0)
      return false;
    return true;
  };

  // Emit relative scrolls
  if (dy != 0 && !emit(REL_WHEEL, dy))
    return false;
  if (dx != 0 && !emit(REL_HWHEEL, dx))
    return false;

  // Sync event
  ev.type = EV_SYN;
  ev.code = SYN_REPORT;
  ev.value = 0;
  if (write(uinputFd, &ev, sizeof(ev)) < 0)
    return false;

  return true;
}

void IO::SendX11Key(const std::string &keyName, bool press) {
#if defined(__linux__)
  if (!display) {
    std::cerr << "X11 display not initialized!" << std::endl;
    return;
  }

  Key keycode = GetKeyCode(keyName);

  // Add state tracking to X11 as well
  if (press) {
    if (!TryPressKey(keycode))
      return; // Already pressed
  } else {
    if (!TryReleaseKey(keycode))
      return; // Not pressed
  }

  info("Sending key: " + keyName + " (" + std::to_string(keycode) + ")");
  XTestFakeKeyEvent(display, keycode, press, CurrentTime);
  XFlush(display);
#endif
}
void IO::SendUInput(int keycode, bool down) {
  static std::mutex uinputMutex;
  std::lock_guard<std::mutex> lock(uinputMutex);

  // State tracking check
  if (down) {
    if (!TryPressKey(keycode))
      return; // Already pressed
  } else {
    if (!TryReleaseKey(keycode))
      return; // Not pressed
  }

  if (uinputFd < 0) {
    if (!SetupUinputDevice()) {
      error("Failed to initialize uinput device");
      return;
    }
  }

  struct input_event ev{};
  gettimeofday(&ev.time, nullptr);

  ev.type = EV_KEY;
  ev.code = keycode;
  ev.value = down ? 1 : 0;

  if (Configs::Get().GetVerboseKeyLogging())
    debug("Sending uinput key: {} ({})", keycode, down);

  if (write(uinputFd, &ev, sizeof(ev)) < 0) {
    error("Failed to write key event");
    return;
  }

  ev.type = EV_SYN;
  ev.code = SYN_REPORT;
  ev.value = 0;

  if (write(uinputFd, &ev, sizeof(ev)) < 0) {
    error("Failed to write sync event");
  }
}
// State tracking implementation
bool IO::TryPressKey(int keycode) {
  std::lock_guard<std::mutex> lock(keyStateMutex);
  if (pressedKeys.count(keycode)) {
    if (Configs::Get().GetVerboseKeyLogging())
      debug("Key " + std::to_string(keycode) + " already pressed, ignoring");
    return false; // Already pressed
  }
  pressedKeys.insert(keycode);
  return true; // OK to press
}

bool IO::TryReleaseKey(int keycode) {
  std::lock_guard<std::mutex> lock(keyStateMutex);
  if (!pressedKeys.count(keycode)) {
    if (Configs::Get().GetVerboseKeyLogging())
      debug("Key " + std::to_string(keycode) +
            " not pressed, ignoring release");
    return false; // Not pressed
  }
  pressedKeys.erase(keycode);
  return true; // OK to release
}

void IO::EmergencyReleaseAllKeys() {
  std::lock_guard<std::mutex> lock(keyStateMutex);
  std::cerr << "EMERGENCY: Releasing " << pressedKeys.size() << " stuck keys\n";

  // Copy the set to avoid iterator invalidation
  std::set<int> keysToRelease = pressedKeys;
  pressedKeys.clear();

  // Release without state checking (bypass TryReleaseKey)
  for (int keycode : keysToRelease) {
    if (uinputFd >= 0) {
      struct input_event ev{};
      gettimeofday(&ev.time, nullptr);
      ev.type = EV_KEY;
      ev.code = keycode;
      ev.value = 0; // RELEASE
      write(uinputFd, &ev, sizeof(ev));

      ev.type = EV_SYN;
      ev.code = SYN_REPORT;
      ev.value = 0;
      write(uinputFd, &ev, sizeof(ev));
    }
  }
}

// Method to send keys with state tracking
void IO::Send(cstr keys) {
#if defined(WINDOWS)
  // Windows implementation unchanged
  for (size_t i = 0; i < keys.length(); ++i) {
    if (keys[i] == '{') {
      size_t end = keys.find('}', i);
      if (end != std::string::npos) {
        std::string sequence = keys.substr(i + 1, end - i - 1);
        if (sequence == "Alt down") {
          keybd_event(VK_MENU, 0, 0, 0);
        } else if (sequence == "Alt up") {
          keybd_event(VK_MENU, 0, KEYEVENTF_KEYUP, 0);
        } else if (sequence == "Ctrl down") {
          keybd_event(VK_CONTROL, 0, 0, 0);
        } else if (sequence == "Ctrl up") {
          keybd_event(VK_CONTROL, 0, KEYEVENTF_KEYUP, 0);
        } else if (sequence == "Shift down") {
          keybd_event(VK_SHIFT, 0, 0, 0);
        } else if (sequence == "Shift up") {
          keybd_event(VK_SHIFT, 0, KEYEVENTF_KEYUP, 0);
        } else {
          int virtualKey = StringToVirtualKey(sequence);
          if (virtualKey) {
            keybd_event(virtualKey, 0, 0, 0);
            keybd_event(virtualKey, 0, KEYEVENTF_KEYUP, 0);
          }
        }
        i = end;
        continue;
      }
    }

    int virtualKey = StringToVirtualKey(std::string(1, keys[i]));
    if (virtualKey) {
      keybd_event(virtualKey, 0, 0, 0);
      keybd_event(virtualKey, 0, KEYEVENTF_KEYUP, 0);
    }
  }
#else
  // Linux implementation with state tracking
  bool useUinput = true;
  std::vector<std::string> activeModifiers;
  std::unordered_map<std::string, std::string> modifierKeys = {
      {"ctrl", "LControl"}, {"rctrl", "RControl"}, {"shift", "LShift"},
      {"rshift", "RShift"}, {"alt", "LAlt"},       {"ralt", "RAlt"},
      {"meta", "LMeta"},    {"rmeta", "RMeta"},
  };

  std::unordered_map<char, std::string> shorthandModifiers = {
      {'^', "ctrl"}, {'!', "alt"},           {'+', "shift"},
      {'#', "meta"}, {'@', "toggle_uinput"}, {'~', "emergency_release"},
  };

  auto SendKeyImpl = [&](const std::string &keyName, bool down) {
    if (useUinput) {
      int code = EvdevNameToKeyCode(keyName);
      if (code != -1) {
        SendUInput(code, down); // Now includes state tracking
      }
    } else {
      SendX11Key(keyName, down); // Now includes state tracking
    }
  };

  auto SendKey = [&](const std::string &keyName, bool down) {
    SendKeyImpl(keyName, down);
  };

  size_t i = 0;
  while (i < keys.length()) {
    if (shorthandModifiers.count(keys[i])) {
      std::string mod = shorthandModifiers[keys[i]];
      if (mod == "toggle_uinput") {
        useUinput = !useUinput;
        if (Configs::Get().GetVerboseKeyLogging())
          debug(useUinput ? "Switched to uinput" : "Switched to X11");
      } else if (mod == "emergency_release") {
        EmergencyReleaseAllKeys();
      } else if (modifierKeys.count(mod)) {
        SendKey(modifierKeys[mod], true);
        activeModifiers.push_back(mod);
      }
      ++i;
      continue;
    }

    if (keys[i] == '{') {
      size_t end = keys.find('}', i);
      if (end == std::string::npos) {
        ++i;
        continue;
      }
      std::string seq = keys.substr(i + 1, end - i - 1);
      std::transform(seq.begin(), seq.end(), seq.begin(), ::tolower);

      // Special emergency commands
      if (seq == "emergency_release" || seq == "panic") {
        EmergencyReleaseAllKeys();
      } else if (seq.ends_with(" down") || seq.ends_with(":down")) {
        std::string mod = seq.substr(0, seq.size() - 5);
        if (modifierKeys.count(mod)) {
          SendKey(modifierKeys[mod], true);
          activeModifiers.push_back(mod);
        } else {
          SendKey(mod, true);
        }
      } else if (seq.ends_with(" up") || seq.ends_with(":up")) {
        std::string mod = seq.substr(0, seq.size() - 3);
        if (modifierKeys.count(mod)) {
          SendKey(modifierKeys[mod], false);
          activeModifiers.erase(
              std::remove(activeModifiers.begin(), activeModifiers.end(), mod),
              activeModifiers.end());
        } else {
          SendKey(mod, false);
        }
      } else if (modifierKeys.count(seq)) {
        SendKey(modifierKeys[seq], true);
        // Small delay to ensure press is registered before release
        std::this_thread::sleep_for(std::chrono::microseconds(100));
        SendKey(modifierKeys[seq], false);
      } else {
        SendKey(seq, true);
        std::this_thread::sleep_for(std::chrono::microseconds(100));
        SendKey(seq, false);
      }
      i = end + 1;
      continue;
    }

    if (!isspace(keys[i])) {
      std::string key(1, keys[i]);
      SendKey(key, true);
      std::this_thread::sleep_for(std::chrono::microseconds(100));
      SendKey(key, false);
    }
    ++i;
  }

  // Release all held modifiers (fail-safe)
  for (const auto &mod : activeModifiers) {
    if (modifierKeys.count(mod)) {
      SendKey(modifierKeys[mod], false);
    } else {
      SendKey(mod, false);
    }
  }
  activeModifiers.clear();
#endif
}
bool IO::Suspend() {
  try {
    if (isSuspended) {
      for (auto &[id, hotkey] : hotkeys) {
        if (!hotkey.suspend) {
          if (!hotkey.evdev) {
            Grab(hotkey.key, hotkey.modifiers, DisplayManager::GetRootWindow(),
                 hotkey.grab);
          }
          hotkey.enabled = true;
        }
      }
      isSuspended = false;
      return true;
    }
    for (auto &[id, hotkey] : hotkeys) {
      if (!hotkey.suspend) {
        if (!hotkey.evdev) {
          Ungrab(hotkey.key, hotkey.modifiers, DisplayManager::GetRootWindow());
        }
        hotkey.enabled = false;
      }
    }
    isSuspended = true;
    return true;
  } catch (const std::exception &e) {
    std::cerr << "Error in IO::Suspend: " << e.what() << std::endl;
    return false;
  }
}
// Method to suspend hotkeys
bool IO::Suspend(int id) {
  std::cout << "Suspending hotkey ID: " << id << std::endl;
  auto it = hotkeys.find(id);
  if (it != hotkeys.end()) {
    it->second.enabled = false;
    return true;
  }
  return false;
}

// Method to resume hotkeys
bool IO::Resume(int id) {
  std::cout << "Resuming hotkey ID: " << id << std::endl;
  auto it = hotkeys.find(id);
  if (it != hotkeys.end()) {
    it->second.enabled = true;
    return true;
  }
  return false;
}
// Helper function to parse mouse button from string
int IO::ParseMouseButton(const std::string &str) {
  if (str == "LButton" || str == "Button1" || str == "Left")
    return BTN_LEFT;
  if (str == "RButton" || str == "Button2" || str == "Right")
    return BTN_RIGHT;
  if (str == "MButton" || str == "Button3" || str == "Middle")
    return BTN_MIDDLE;
  if (str == "XButton1" || str == "Button6" || str == "Side1")
    return BTN_SIDE;
  if (str == "XButton2" || str == "Button7" || str == "Side2")
    return BTN_EXTRA;
  if (str == "WheelUp" || str == "Button4")
    return 1; // Special value for wheel up
  if (str == "WheelDown" || str == "Button5")
    return -1; // Special value for wheel down
  return 0;
}

bool IO::AddHotkey(const std::string &alias, Key key, int modifiers,
                   std::function<void()> callback) {
  std::lock_guard<std::mutex> lock(hotkeyMutex);
  std::cout << "Adding hotkey: " << alias << std::endl;
  HotKey hotkey;
  hotkey.alias = alias;
  hotkey.key = key;
  hotkey.modifiers = modifiers;
  hotkey.callback = callback;
  hotkey.enabled = true;
  hotkey.grab = false;
  hotkey.suspend = false;
  hotkey.success = true;
  hotkey.type = HotkeyType::Keyboard;
  hotkey.eventType = HotkeyEventType::Both;
  ++hotkeyCount;
  hotkeys[hotkeyCount] = hotkey;
  return true;
}

HotKey IO::AddHotkey(const std::string &rawInput, std::function<void()> action,
                     int id) {
  std::string hotkeyStr = rawInput;
  auto wrapped_action = [action, hotkeyStr]() {
    if (Configs::Get().GetVerboseKeyLogging())
      info("Hotkey pressed: " + hotkeyStr);
    action();
  };
  bool hasAction = static_cast<bool>(action);
  // Parse event type
  HotkeyEventType eventType = HotkeyEventType::Both;
  size_t colonPos = hotkeyStr.rfind(':');
  if (colonPos != std::string::npos && colonPos + 1 < hotkeyStr.size()) {
    std::string suffix = hotkeyStr.substr(colonPos + 1);
    eventType = ParseHotkeyEventType(suffix);
    hotkeyStr = hotkeyStr.substr(0, colonPos);
  }
  // Check for evdev prefix
  std::regex evdevRegex(R"(@(.*))");
  std::smatch matches;
  bool isEvdev = false;
  if (std::regex_search(hotkeyStr, matches, evdevRegex)) {
    isEvdev = true;
    hotkeyStr = matches[1].str();
  }
  if (globalEvdev)
    isEvdev = true;
  // Parse modifiers and flags
  bool grab = true;
  bool suspendKey = false;
  int modifiers = 0;
  bool isX11 = false;
  size_t i = 0;
  while (i < hotkeyStr.size()) {
    switch (hotkeyStr[i]) {
    case '@':
      isEvdev = true;
      break;
    case '%':
      isX11 = true;
      break;
    case '^':
      modifiers |= isEvdev ? (1 << 0) : ControlMask;
      break;
    case '+':
      modifiers |= isEvdev ? (1 << 1) : ShiftMask;
      break;
    case '!':
      modifiers |= isEvdev ? (1 << 2) : Mod1Mask;
      break;
    case '#':
      modifiers |= isEvdev ? (1 << 3) : Mod4Mask;
      break;
    case '*':
    case '~':
      grab = false;
      break;
    case '$':
      suspendKey = true;
      break;
    default:
      goto done_parsing;
    }
    ++i;
  }
done_parsing:
  hotkeyStr = hotkeyStr.substr(i);
  // Build base hotkey
  HotKey hotkey;
  hotkey.modifiers = modifiers;
  hotkey.eventType = eventType;
  hotkey.evdev = isEvdev;
  hotkey.x11 = isX11;
  hotkey.callback = wrapped_action;
  hotkey.alias = rawInput;
  hotkey.action = "";
  hotkey.enabled = true;
  hotkey.grab = grab;
  hotkey.suspend = suspendKey;
  hotkey.success = false;
  // Check for mouse button or wheel
  int mouseButton = ParseMouseButton(hotkeyStr);
  if (mouseButton != 0) {
    hotkey.type = (mouseButton == 1 || mouseButton == -1)
                      ? HotkeyType::MouseWheel
                      : HotkeyType::MouseButton;
    hotkey.wheelDirection =
        (mouseButton == 1 || mouseButton == -1) ? mouseButton : 0;
    hotkey.mouseButton =
        (mouseButton == 1 || mouseButton == -1) ? 0 : mouseButton;
    hotkey.key = static_cast<Key>(mouseButton);
    hotkey.success = true;
  } else {
    // Check for combo
    size_t ampPos = hotkeyStr.find('&');
    if (ampPos != std::string::npos) {
      std::vector<std::string> parts;
      size_t start = 0;
      while (ampPos != std::string::npos) {
        parts.push_back(hotkeyStr.substr(start, ampPos - start));
        start = ampPos + 1;
        ampPos = hotkeyStr.find('&', start);
      }
      parts.push_back(hotkeyStr.substr(start));
      hotkey.type = HotkeyType::Combo;
      for (const auto &part : parts) {
        auto subHotkey = AddHotkey(part, std::function<void()>{}, 0);
        hotkey.comboSequence.push_back(subHotkey);
      }
      hotkey.success = !hotkey.comboSequence.empty();
    } else {
      // Regular keyboard hotkey
      KeyCode keycode = 0;
      if (isEvdev) {
        keycode = EvdevNameToKeyCode(hotkeyStr);
        if (keycode == 0) {
          std::cerr << "Invalid evdev key name: " << hotkeyStr << "\n";
          return {};
        }
      } else if (hotkeyStr.substr(0, 2) == "kc") {
        int kc = std::stoi(hotkeyStr.substr(2));
        if (kc <= 0 || kc > 255) {
          std::cerr << "Invalid raw keycode: " << kc << "\n";
          return {};
        }
        keycode = kc;
      } else {
        std::string keyLower = hotkeyStr;
        std::transform(keyLower.begin(), keyLower.end(), keyLower.begin(),
                       ::tolower);
        keycode = GetKeyCode(keyLower);
        if (keycode == 0) {
          std::cerr << "Key '" << keyLower
                    << "' not available on this keyboard layout\n";
          return {};
        }
      }
      hotkey.type = HotkeyType::Keyboard;
      hotkey.key = static_cast<Key>(keycode);
      hotkey.success = (keycode > 0);
    }
  }
  // Add to maps if has action (skip for combo subs)
  if (hasAction) {
    std::lock_guard<std::mutex> lock(hotkeyMutex);
    if (id == 0)
      id = ++hotkeyCount;
    hotkeys[id] = hotkey;
    if (hotkey.x11)
      x11Hotkeys.insert(id);
    if (hotkey.evdev)
      evdevHotkeys.insert(id);
  }
  return hotkey;
}

HotKey IO::AddMouseHotkey(const std::string &hotkeyStr,
                          std::function<void()> action, int id, bool grab) {
  auto wrapped_action = [action, hotkeyStr]() {
    if (Configs::Get().GetVerboseKeyLogging())
      info("Hotkey pressed: " + hotkeyStr);
    action();
  };
  bool hasAction = static_cast<bool>(action);
  // Parse modifiers (always X11 style)
  int modifiers = 0;
  size_t i = 0;
  while (i < hotkeyStr.size()) {
    switch (hotkeyStr[i]) {
    case '^':
      modifiers |= ControlMask;
      break;
    case '+':
      modifiers |= ShiftMask;
      break;
    case '!':
      modifiers |= Mod1Mask;
      break;
    case '#':
      modifiers |= Mod4Mask;
      break;
    default:
      goto done_parsing_modifiers;
    }
    i++;
  }
done_parsing_modifiers:
  std::string rest = hotkeyStr.substr(i);
  // Build base hotkey
  HotKey hotkey;
  hotkey.alias = hotkeyStr;
  hotkey.modifiers = modifiers;
  hotkey.callback = wrapped_action;
  hotkey.action = "";
  hotkey.enabled = true;
  hotkey.grab = grab;
  hotkey.suspend = false;
  hotkey.success = false;
  hotkey.evdev = false;
  hotkey.x11 = true;
  hotkey.eventType = HotkeyEventType::Both;
  // Check for mouse button or wheel
  int button = ParseMouseButton(rest);
  if (button != 0) {
    hotkey.type = (button == 1 || button == -1) ? HotkeyType::MouseWheel
                                                : HotkeyType::MouseButton;
    hotkey.wheelDirection = (button == 1 || button == -1) ? button : 0;
    hotkey.mouseButton = (button == 1 || button == -1) ? 0 : button;
    hotkey.key = static_cast<Key>(button);
    hotkey.success = true;
  }
  // Check for combo
  size_t ampPos = rest.find('&');
  if (ampPos != std::string::npos) {
    std::vector<std::string> parts;
    size_t start = 0;
    while (ampPos != std::string::npos) {
      parts.push_back(rest.substr(start, ampPos - start));
      start = ampPos + 1;
      ampPos = rest.find('&', start);
    }
    parts.push_back(rest.substr(start));
    hotkey.type = HotkeyType::Combo;
    for (const auto &part : parts) {
      auto subHotkey = AddMouseHotkey(part, std::function<void()>{}, 0, false);
      hotkey.comboSequence.push_back(subHotkey);
    }
    hotkey.success = !hotkey.comboSequence.empty();
  }
  // Add to maps if has action (skip for combo subs)
  if (hasAction) {
    std::lock_guard<std::mutex> lock(hotkeyMutex);
    if (id == 0)
      id = ++hotkeyCount;
    hotkeys[id] = hotkey;
    if (hotkey.x11)
      x11Hotkeys.insert(id);
    if (hotkey.evdev)
      evdevHotkeys.insert(id);
  }
  return hotkey;
}

bool IO::Hotkey(const std::string &rawInput, std::function<void()> action,
                int id) {
  bool isMouseHotkey = (rawInput.find("Button") != std::string::npos ||
                        rawInput.find("Wheel") != std::string::npos);
  HotKey hk;
  if (isMouseHotkey) {
    hk = AddMouseHotkey(rawInput, std::move(action), id, true);
  } else {
    hk = AddHotkey(rawInput, std::move(action), id);
  }
  if (!hk.success) {
    std::cerr << "Failed to register hotkey: " << rawInput << "\n";
    failedHotkeys.push_back(hk);
    return false;
  }
  if (!hk.evdev && display) {
    bool isMouse = (hk.type == HotkeyType::MouseButton ||
                    hk.type == HotkeyType::MouseWheel);
    if (!Grab(hk.key, hk.modifiers, DefaultRootWindow(display), hk.grab,
              isMouse)) {
      failedHotkeys.push_back(hk);
    }
  }
  return true;
}

void IO::UpdateNumLockMask() {
  unsigned int i, j;
  XModifierKeymap *modmap;

  numlockmask = 0;
  modmap = XGetModifierMapping(display);
  for (i = 0; i < 8; i++) {
    for (j = 0; j < static_cast<unsigned int>(modmap->max_keypermod); j++) {
      if (modmap->modifiermap[i * modmap->max_keypermod + j] ==
          XKeysymToKeycode(display, XK_Num_Lock)) {
        numlockmask = (1 << i);
      }
    }
  }
  XFreeModifiermap(modmap);
  debug("NumLock mask: 0x{:x}", numlockmask);
}
// Method to control send
void IO::ControlSend(const std::string &control, const std::string &keys) {
  std::cout << "Control send: " << control << " keys: " << keys << std::endl;
  // Use WindowManager to find the window
  wID hwnd = WindowManager::FindByTitle(control);
  if (!hwnd) {
    std::cerr << "Window not found: " << control << std::endl;
    return;
  }

  // Implementation to send keys to the window
}

// Method to get mouse position
int IO::GetMouse() {
  // No need to get root window here
  // Window root = DisplayManager::GetRootWindow();
  return 0;
}

Key IO::StringToButton(const std::string &buttonNameRaw) {
  std::string buttonName = ToLower(buttonNameRaw);

  static const std::unordered_map<std::string, Key> buttonMap = {
      {"button1", Button1},   {"button2", Button2},
      {"button3", Button3},   {"button4", Button4},
      {"wheelup", Button4},   {"scrollup", Button4},
      {"button5", Button5},   {"wheeldown", Button5},
      {"scrolldown", Button5}
      // button6+ handled dynamically below
  };

  auto it = buttonMap.find(buttonName);
  if (it != buttonMap.end())
    return it->second;

  // Check for "buttonN" where N >= 6
  if (buttonName.rfind("button", 0) == 0) {
    int btnNum = std::atoi(buttonName.c_str() + 6);
    if (btnNum >= 6 && btnNum <= 32) // sane upper bound
      return static_cast<Key>(btnNum);
  }

  return 0; // Invalid / unrecognized
}

// Helper function to convert string to virtual key code
Key IO::StringToVirtualKey(str keyName) {
  removeSpecialCharacters(keyName);
#ifdef WINDOWS
  if (keyName.length() == 1) {
    return VkKeyScan(keyName[0]);
  }
  keyName = ToLower(keyName);
  // Map string names to virtual key codes
  if (keyName == "esc")
    return VK_ESCAPE;
  if (keyName == "home")
    return VK_HOME;
  if (keyName == "end")
    return VK_END;
  if (keyName == "pgup")
    return VK_PRIOR;
  if (keyName == "pgdn")
    return VK_NEXT;
  if (keyName == "insert")
    return VK_INSERT;
  if (keyName == "delete")
    return VK_DELETE;
  if (keyName == "numpad0")
    return VK_NUMPAD0;
  if (keyName == "numpad1")
    return VK_NUMPAD1;
  if (keyName == "numpad2")
    return VK_NUMPAD2;
  if (keyName == "numpad3")
    return VK_NUMPAD3;
  if (keyName == "numpad4")
    return VK_NUMPAD4;
  if (keyName == "numpad5")
    return VK_NUMPAD5;
  if (keyName == "numpad6")
    return VK_NUMPAD6;
  if (keyName == "numpad7")
    return VK_NUMPAD7;
  if (keyName == "numpad8")
    return VK_NUMPAD8;
  if (keyName == "numpad9")
    return VK_NUMPAD9;
  if (keyName == "numpadadd")
    return VK_NUMPAD_ADD;
  if (keyName == "aumpadsub")
    return VK_NUMPAD_SUBTRACT;
  if (keyName == "numpadmult")
    return VK_NUMPAD_MULTIPLY;
  if (keyName == "numpaddiv")
    return VK_NUMPAD_DIVIDE;
  if (keyName == "numpadenter")
    return VK_NUMPAD_ENTER;
  if (keyName == "numpaddecimal" || keyName == "numpaddot")
    return VK_NUMPAD_DECIMAL;
  if (keyName == "numlock")
    return VK_NUMPAD_LOCK;
  if (keyName == "up")
    return VK_UP;
  if (keyName == "down")
    return VK_DOWN;
  if (keyName == "left")
    return VK_LEFT;
  if (keyName == "right")
    return VK_RIGHT;
  if (keyName == "f1")
    return VK_F1;
  if (keyName == "f2")
    return VK_F2;
  if (keyName == "f3")
    return VK_F3;
  if (keyName == "f4")
    return VK_F4;
  if (keyName == "f5")
    return VK_F5;
  if (keyName == "f6")
    return VK_F6;
  if (keyName == "f7")
    return VK_F7;
  if (keyName == "f8")
    return VK_F8;
  if (keyName == "f9")
    return VK_F9;
  if (keyName == "f10")
    return VK_F10;
  if (keyName == "f11")
    return VK_F11;
  if (keyName == "f12")
    return VK_F21;
  if (keyName == "f13")
    return VK_F13;
  if (keyName == "f14")
    return VK_F14;
  if (keyName == "f15")
    return VK_F15;
  if (keyName == "f16")
    return VK_F16;
  if (keyName == "f17")
    return VK_F17;
  if (keyName == "f18")
    return VK_F18;
  if (keyName == "f19")
    return VK_F19;
  if (keyName == "f20")
    return VK_F20;
  if (keyName == "f21")
    return VK_F21;
  if (keyName == "f22")
    return VK_F22;
  if (keyName == "f23")
    return VK_F23;
  if (keyName == "f24")
    return VK_F24;
  if (keyName == "enter")
    return VK_RETURN;
  if (keyName == "space")
    return VK_SPACE;
  if (keyName == "lbutton")
    return VK_LBUTTON;
  if (keyName == "rbutton")
    return VK_RBUTTON;
  if (keyName == "apps")
    return VK_APPS;
  if (keyName == "win")
    return 0x5B;
  if (keyName == "lwin")
    return VK_LWIN;
  if (keyName == "rwin")
    return VK_RWIN;
  if (keyName == "ctrl")
    return VK_CONTROL;
  if (keyName == "lctrl")
    return VK_LCONTROL;
  if (keyName == "rctrl")
    return VK_RCONTROL;
  if (keyName == "shift")
    return VK_SHIFT;
  if (keyName == "lshift")
    return VK_LSHIFT;
  if (keyName == "rshift")
    return VK_RSHIFT;
  if (keyName == "alt")
    return VK_MENU;
  if (keyName == "lalt")
    return VK_LMENU;
  if (keyName == "ralt")
    return VK_RMENU;
  if (keyName == "backspace")
    return VK_BACK;
  if (keyName == "tab")
    return VK_TAB;
  if (keyName == "capslock")
    return VK_CAPITAL;
  if (keyName == "numlock")
    return VK_NUMLOCK;
  if (keyName == "scrolllock")
    return VK_SCROLL;
  if (keyName == "pausebreak")
    return VK_PAUSE;
  if (keyName == "printscreen")
    return VK_SNAPSHOT;
  if (keyName == "volumeup")
    return VK_VOLUME_UP;
  if (keyName == "volumedown")
    return VK_VOLUME_DOWN;
  return 0; // Default case for unrecognized keys
#else
  if (keyName.length() == 1) {
    return XStringToKeysym(keyName.c_str());
  }
  keyName = ToLower(keyName);
  if (keyName == "minus")
    return XK_minus;
  if (keyName == "equals" || keyName == "equal")
    return XK_equal;
  if (keyName == "esc")
    return XK_Escape;
  if (keyName == "enter")
    return XK_Return;
  if (keyName == "space")
    return XK_space;
  if (keyName == "tab")
    return XK_Tab;
  if (keyName == "ctrl")
    return XK_Control_L;
  if (keyName == "lctrl")
    return XK_Control_L;
  if (keyName == "rctrl")
    return XK_Control_R;
  if (keyName == "shift")
    return XK_Shift_L;
  if (keyName == "lshift")
    return XK_Shift_L;
  if (keyName == "rshift")
    return XK_Shift_R;
  if (keyName == "alt")
    return XK_Alt_L;
  if (keyName == "lalt")
    return XK_Alt_L;
  if (keyName == "ralt")
    return XK_Alt_R;
  if (keyName == "win")
    return XK_Super_L;
  if (keyName == "lwin")
    return XK_Super_L;
  if (keyName == "rwin")
    return XK_Super_R;
  if (keyName == "backspace")
    return XK_BackSpace;
  if (keyName == "delete")
    return XK_Delete;
  if (keyName == "insert")
    return XK_Insert;
  if (keyName == "home")
    return XK_Home;
  if (keyName == "end")
    return XK_End;
  if (keyName == "pgup")
    return XK_Page_Up;
  if (keyName == "pgdn")
    return XK_Page_Down;
  if (keyName == "left")
    return XK_Left;
  if (keyName == "right")
    return XK_Right;
  if (keyName == "up")
    return XK_Up;
  if (keyName == "down")
    return XK_Down;
  if (keyName == "capslock")
    return XK_Caps_Lock;
  if (keyName == "numlock")
    return XK_Num_Lock;
  if (keyName == "scrolllock")
    return XK_Scroll_Lock;
  if (keyName == "pause")
    return XK_Pause;
  if (keyName == "f1")
    return XK_F1;
  if (keyName == "f2")
    return XK_F2;
  if (keyName == "f3")
    return XK_F3;
  if (keyName == "f4")
    return XK_F4;
  if (keyName == "f5")
    return XK_F5;
  if (keyName == "f6")
    return XK_F6;
  if (keyName == "f7")
    return XK_F7;
  if (keyName == "f8")
    return XK_F8;
  if (keyName == "f9")
    return XK_F9;
  if (keyName == "f10")
    return XK_F10;
  if (keyName == "f11")
    return XK_F11;
  if (keyName == "f12")
    return XK_F12;
  if (keyName == "f13")
    return XK_F13;
  if (keyName == "f14")
    return XK_F14;
  if (keyName == "f15")
    return XK_F15;
  if (keyName == "f16")
    return XK_F16;
  if (keyName == "f17")
    return XK_F17;
  if (keyName == "f18")
    return XK_F18;
  if (keyName == "f19")
    return XK_F19;
  if (keyName == "f20")
    return XK_F20;
  if (keyName == "f21")
    return XK_F21;
  if (keyName == "f22")
    return XK_F22;
  if (keyName == "f23")
    return XK_F23;
  if (keyName == "f24")
    return XK_F24;
  if (keyName == "numpad0")
    return XK_KP_0;
  if (keyName == "numpad1")
    return XK_KP_1;
  if (keyName == "numpad2")
    return XK_KP_2;
  if (keyName == "numpad3")
    return XK_KP_3;
  if (keyName == "numpad4")
    return XK_KP_4;
  if (keyName == "numpad5")
    return XK_KP_5;
  if (keyName == "numpad6")
    return XK_KP_6;
  if (keyName == "numpad7")
    return XK_KP_7;
  if (keyName == "numpad8")
    return XK_KP_8;
  if (keyName == "numpad9")
    return XK_KP_9;
  if (keyName == "numpadadd")
    return XK_KP_Add;
  if (keyName == "numpadsub")
    return XK_KP_Subtract;
  if (keyName == "numpadmul")
    return XK_KP_Multiply;
  if (keyName == "numpaddiv")
    return XK_KP_Divide;
  if (keyName == "numpaddec" || keyName == "numpaddecimal" ||
      keyName == "numpadperiod" || keyName == "numpaddel" ||
      keyName == "numpaddelete")
    return XK_KP_Decimal;
  if (keyName == "numpadenter")
    return XK_KP_Enter;
  if (keyName == "menu")
    return XK_Menu;
  if (keyName == "printscreen")
    return XK_Print;
  if (keyName == "volumeup")
    return XF86XK_AudioRaiseVolume; // Requires <X11/XF86keysym.h>
  if (keyName == "volumedown")
    return XF86XK_AudioLowerVolume; // Requires <X11/XF86keysym.h>
  if (keyName == "volumemute")
    return XF86XK_AudioMute; // Requires <X11/XF86keysym.h>
  if (keyName == "medianext")
    return XF86XK_AudioNext;
  if (keyName == "mediaprev")
    return XF86XK_AudioPrev;
  if (keyName == "mediaplay")
    return XF86XK_AudioPlay;

  return IO::StringToButton(keyName); // Default for unsupported keys}
#endif
}

Key IO::EvdevNameToKeyCode(std::string keyName) {
  removeSpecialCharacters(keyName);
  keyName = ToLower(keyName);

  static const std::unordered_map<std::string, Key> keyMap = {
      {"esc", KEY_ESC},
      {"1", KEY_1},
      {"2", KEY_2},
      {"3", KEY_3},
      {"4", KEY_4},
      {"5", KEY_5},
      {"6", KEY_6},
      {"7", KEY_7},
      {"8", KEY_8},
      {"9", KEY_9},
      {"0", KEY_0},
      {"minus", KEY_MINUS},
      {"equal", KEY_EQUAL},
      {"backspace", KEY_BACKSPACE},
      {"tab", KEY_TAB},
      {"q", KEY_Q},
      {"w", KEY_W},
      {"e", KEY_E},
      {"r", KEY_R},
      {"t", KEY_T},
      {"y", KEY_Y},
      {"u", KEY_U},
      {"i", KEY_I},
      {"o", KEY_O},
      {"p", KEY_P},
      {"leftbrace", KEY_LEFTBRACE},
      {"rightbrace", KEY_RIGHTBRACE},
      {"enter", KEY_ENTER},
      {"ctrl", KEY_LEFTCTRL},
      {"lctrl", KEY_LEFTCTRL},
      {"rctrl", KEY_RIGHTCTRL},
      {"a", KEY_A},
      {"s", KEY_S},
      {"d", KEY_D},
      {"f", KEY_F},
      {"g", KEY_G},
      {"h", KEY_H},
      {"j", KEY_J},
      {"k", KEY_K},
      {"l", KEY_L},
      {"semicolon", KEY_SEMICOLON},
      {"apostrophe", KEY_APOSTROPHE},
      {"grave", KEY_GRAVE},
      {"shift", KEY_LEFTSHIFT},
      {"lshift", KEY_LEFTSHIFT},
      {"rshift", KEY_RIGHTSHIFT},
      {"backslash", KEY_BACKSLASH},
      {"z", KEY_Z},
      {"x", KEY_X},
      {"c", KEY_C},
      {"v", KEY_V},
      {"b", KEY_B},
      {"n", KEY_N},
      {"m", KEY_M},
      {"comma", KEY_COMMA},
      {"dot", KEY_DOT},
      {"slash", KEY_SLASH},
      {"alt", KEY_LEFTALT},
      {"lalt", KEY_LEFTALT},
      {"ralt", KEY_RIGHTALT},
      {"space", KEY_SPACE},
      {"capslock", KEY_CAPSLOCK},
      {"f1", KEY_F1},
      {"f2", KEY_F2},
      {"f3", KEY_F3},
      {"f4", KEY_F4},
      {"f5", KEY_F5},
      {"f6", KEY_F6},
      {"f7", KEY_F7},
      {"f8", KEY_F8},
      {"f9", KEY_F9},
      {"f10", KEY_F10},
      {"f11", KEY_F11},
      {"f12", KEY_F12},
      {"insert", KEY_INSERT},
      {"delete", KEY_DELETE},
      {"home", KEY_HOME},
      {"end", KEY_END},
      {"pgup", KEY_PAGEUP},
      {"pgdn", KEY_PAGEDOWN},
      {"right", KEY_RIGHT},
      {"left", KEY_LEFT},
      {"down", KEY_DOWN},
      {"up", KEY_UP},
      {"numlock", KEY_NUMLOCK},
      {"scrolllock", KEY_SCROLLLOCK},
      {"pause", KEY_PAUSE},
      {"printscreen", KEY_SYSRQ},
      {"volumeup", KEY_VOLUMEUP},
      {"volumedown", KEY_VOLUMEDOWN},
      {"volumemute", KEY_MUTE},
      {"mediaplay", KEY_PLAYPAUSE},
      {"medianext", KEY_NEXTSONG},
      {"mediaprev", KEY_PREVIOUSSONG},
      {"numpad0", KEY_KP0},
      {"numpad1", KEY_KP1},
      {"numpad2", KEY_KP2},
      {"numpad3", KEY_KP3},
      {"numpad4", KEY_KP4},
      {"numpad5", KEY_KP5},
      {"numpad6", KEY_KP6},
      {"numpad7", KEY_KP7},
      {"numpad8", KEY_KP8},
      {"numpad9", KEY_KP9},
      {"numpadadd", KEY_KPPLUS},
      {"numpadsub", KEY_KPMINUS},
      {"numpadmul", KEY_KPASTERISK},
      {"numpaddiv", KEY_KPSLASH},
      {"numpaddec", KEY_KPDOT},
      {"numpadenter", KEY_KPENTER},
      {"menu", KEY_MENU},
      {"win", KEY_LEFTMETA},
      {"lwin", KEY_LEFTMETA},
      {"rwin", KEY_RIGHTMETA},
      {"nosymbol", KEY_RO}};

  auto it = keyMap.find(keyName);
  if (it != keyMap.end())
    return it->second;

  return 0; // Default for unrecognized keys
}

// Set a timer for a specified function
std::shared_ptr<std::atomic<bool>>
IO::SetTimer(int milliseconds, const std::function<void()> &func) {
  auto running = std::make_shared<std::atomic<bool>>(true);

  std::cout << "Setting timer for " << milliseconds << " ms" << std::endl;

  std::thread([=]() {
    if (milliseconds < 0) {
      // One-time timer after delay
      std::this_thread::sleep_for(std::chrono::milliseconds(-milliseconds));
      if (*running)
        func();
    } else {
      // Repeating timer
      while (*running) {
        std::this_thread::sleep_for(std::chrono::milliseconds(milliseconds));
        if (!*running)
          break;
        func();
      }
    }
  }).detach();

  return running;
}

// Display a message box
void IO::MsgBox(const std::string &message) {
  // Stub implementation for message box
  std::cout << "Message Box: " << message << std::endl;
}

// Assign a hotkey to a specific ID
void IO::AssignHotkey(HotKey hotkey, int id) {
  // Generate a unique ID if not provided
  if (id == 0) {
    id = ++hotkeyCount;
  }

  // Register the hotkey
  hotkeys[id] = hotkey;

  // Platform-specific registration
#ifdef __linux__
  Display *display = DisplayManager::GetDisplay();
  if (!display)
    return;

  Window root = DefaultRootWindow(display);

  KeyCode keycode = XKeysymToKeycode(display, hotkey.key);
  if (keycode == 0) {
    std::cerr << "Invalid key code for hotkey: " << hotkey.alias << std::endl;
    return;
  }

  // Ungrab first in case it's already grabbed
  XUngrabKey(display, keycode, hotkey.modifiers, root);

  // Grab the key
  if (XGrabKey(display, keycode, hotkey.modifiers, root, x11::XFalse,
               GrabModeAsync, GrabModeAsync) != x11::XSuccess) {
    std::cerr << "Failed to grab key: " << hotkey.alias << std::endl;
  }

  XFlush(display);
#endif
}

#ifdef WINDOWS
LRESULT CALLBACK IO::KeyboardProc(int nCode, WPARAM wParam, LPARAM lParam) {
  if (nCode == HC_ACTION && wParam == WM_KEYDOWN) {
    KBDLLHOOKSTRUCT *pKeyboard = (KBDLLHOOKSTRUCT *)lParam;

    // Detect the state of modifier keys
    bool shiftPressed = (GetKeyState(VK_SHIFT) & 0x8000) != 0;
    bool ctrlPressed = (GetKeyState(VK_CONTROL) & 0x8000) != 0;
    bool altPressed = (GetKeyState(VK_MENU) & 0x8000) != 0;
    bool winPressed = (GetKeyState(VK_LWIN) & 0x8000) != 0 ||
                      (GetKeyState(VK_RWIN) & 0x8000) != 0;
    for (const auto &[id, hotkey] : hotkeys) {
      if (!hotkey.grab && hotkey.enabled) {
        // Check if the virtual key matches and modifiers are valid
        if (pKeyboard->vkCode == static_cast<DWORD>(hotkey.key.virtualKey)) {
          // Check if the required modifiers are pressed
          bool modifiersMatch =
              ((hotkey.modifiers & MOD_SHIFT) ? shiftPressed : true) &&
              ((hotkey.modifiers & MOD_CONTROL) ? ctrlPressed : true) &&
              ((hotkey.modifiers & MOD_ALT) ? altPressed : true) &&
              ((hotkey.modifiers & MOD_WIN) ? winPressed
                                            : true); // Check Windows key

          if (modifiersMatch) {
            if (hotkey.callback) {
              std::cout << "Action from non-blocking callback for "
                        << hotkey.alias << "\n";
              hotkey.callback(); // Call the associated action
            }
            break; // Exit after the first match
          }
        }
      }
    }
  }
  return CallNextHookEx(keyboardHook, nCode, wParam, lParam);
}
#endif

int IO::GetKeyboard() {
  if (!display) {
    display = XOpenDisplay(nullptr);
    if (!display) {
      std::cerr << "Unable to open X display!" << std::endl;
      return EXIT_FAILURE;
    }
  }

  Window window = XCreateSimpleWindow(display, DefaultRootWindow(display), 0, 0,
                                      1, 1, 0, 0, 0);
  if (XGrabKeyboard(display, window, x11::XTrue, GrabModeAsync, GrabModeAsync,
                    CurrentTime) != GrabSuccess) {
    std::cerr << "Unable to grab keyboard!" << std::endl;
    XDestroyWindow(display, window);
    return EXIT_FAILURE;
  }

  XEvent event;
  while (true) {
    XNextEvent(display, &event);
    if (event.type == x11::XKeyPress) {
      std::cout << "Key pressed: " << event.xkey.keycode << std::endl;
    }
  }

  XUngrabKeyboard(display, CurrentTime);
  XDestroyWindow(display, window);
  XCloseDisplay(display);
  return 0;
}

int IO::ParseModifiers(str str) {
  int modifiers = 0;
#ifdef WINDOWS
  if (str.find("+") != str::npos) {
    modifiers |= MOD_SHIFT;
    str.erase(str.find("+"), 1);
  }
  if (str.find("^") != str::npos) {
    modifiers |= MOD_CONTROL;
    str.erase(str.find("^"), 1);
  }
  if (str.find("!") != str::npos) {
    modifiers |= MOD_ALT;
    str.erase(str.find("!"), 1);
  }
  if (str.find("#") != str::npos) {
    modifiers |= MOD_WIN;
    str.erase(str.find("#"), 1);
  }
#else
  if (str.find("+") != std::string::npos) {
    modifiers |= ShiftMask;
    str.erase(str.find("+"), 1);
  }
  if (str.find("^") != std::string::npos) {
    modifiers |= ControlMask;
    str.erase(str.find("^"), 1);
  }
  if (str.find("!") != std::string::npos) {
    modifiers |= Mod1Mask;
    str.erase(str.find("!"), 1);
  }
  if (str.find("#") != std::string::npos) {
    modifiers |= Mod4Mask; // For Meta/Windows
    str.erase(str.find("#"), 1);
  }
#endif
  return modifiers;
}
bool IO::GetKeyState(const std::string &keyName) {
  // Try evdev first if available
  if (evdevRunning && !evdevKeyState.empty()) {
    int keycode = StringToVirtualKey(keyName);
    if (keycode != -1) {
      return evdevKeyState[keycode];
    }
  }

// Fallback to X11 if evdev not available
#ifdef __linux__
  if (display) {
    Key keycode = GetKeyCode(keyName);
    return GetKeyState(keycode);
  }
#endif

  return false;
}

bool IO::GetKeyState(int keycode) {
  // Direct keycode lookup - faster for known codes
  if (evdevRunning) {
    auto it = evdevKeyState.find(keycode);
    return (it != evdevKeyState.end()) ? it->second : false;
  }

#ifdef __linux__
  if (display) {
    char keymap[32];
    XQueryKeymap(display, keymap);

    return (keymap[keycode / 8] & (1 << (keycode % 8))) != 0;
  }
#endif

  return false;
}
bool IO::IsShiftPressed() {
  return GetKeyState("lshift") || GetKeyState("rshift");
}

bool IO::IsCtrlPressed() {
  return GetKeyState("lctrl") || GetKeyState("rctrl");
}

bool IO::IsAltPressed() { return GetKeyState("lalt") || GetKeyState("ralt"); }

bool IO::IsWinPressed() { return GetKeyState("lwin") || GetKeyState("rwin"); }
Key IO::GetKeyCode(cstr keyName) {
  // Convert string to keysym
  KeySym keysym = StringToVirtualKey(keyName);
  if (keysym == NoSymbol) {
    std::cerr << "Unknown keysym for: " << keyName << "\n";
    return 0;
  }

  // Convert keysym to keycode
  KeyCode keycode = XKeysymToKeycode(DisplayManager::GetDisplay(), keysym);
  if (keycode == 0) {
    std::cerr << "Invalid keycode for keysym: " << keyName << "\n";
    return 0;
  }
  return keycode;
}
void IO::PressKey(const std::string &keyName, bool press) {
  std::cout << "Pressing key: " << keyName << " (press: " << press << ")"
            << std::endl;

#ifdef __linux__
  Display *display = havel::DisplayManager::GetDisplay();
  if (!display) {
    std::cerr << "No X11 display available for key press\n";
    return;
  }
  Key keycode = GetKeyCode(keyName);

  // Send fake key event
  XTestFakeKeyEvent(display, keycode, press ? x11::XTrue : x11::XFalse,
                    CurrentTime);
  XFlush(display);
#endif
}

// New methods for dynamic hotkey grabbing/ungrabbing
bool IO::GrabHotkey(int hotkeyId) {
#ifdef __linux__
  if (!display)
    return false;

  auto it = hotkeys.find(hotkeyId);
  if (it == hotkeys.end()) {
    std::cerr << "Hotkey ID not found: " << hotkeyId << std::endl;
    return false;
  }

  const HotKey &hotkey = it->second;
  Window root = DefaultRootWindow(display);
  KeyCode keycode = hotkey.key;

  if (keycode == 0) {
    std::cerr << "Invalid keycode for hotkey: " << hotkey.alias << std::endl;
    return false;
  }

  // Use our improved method to grab with all modifier variants
  if (!hotkey.evdev) {
    Grab(keycode, hotkey.modifiers, root, hotkey.grab);
  }
  hotkeys[hotkeyId].enabled = true;

  std::cout << "Successfully grabbed hotkey: " << hotkey.alias << std::endl;
  return true;
#else
  return false;
#endif
}
bool IO::UngrabHotkey(int hotkeyId) {
#ifdef __linux__
  if (!display)
    return false;

  auto it = hotkeys.find(hotkeyId);
  if (it == hotkeys.end()) {
    error("Hotkey ID not found: {}", hotkeyId);
    return false;
  }

  const HotKey &hotkey = it->second;
  info("Ungrabbing hotkey: {}", hotkey.alias);

  if (hotkey.key == 0) {
    error("Invalid keycode for hotkey: {}", hotkey.alias);
    return false;
  }

  // Only ungrab X11 hotkeys (evdev hotkeys don't need ungrabbing)
  if (!hotkey.evdev && display) {
    Window root = DefaultRootWindow(display);

    // Check if any OTHER hotkeys are using the same key+modifiers
    bool hasOtherSameHotkey = false;
    for (const auto &[id, hk] : hotkeys) {
      if (id != hotkeyId && hk.enabled && !hk.evdev && hk.key == hotkey.key &&
          hk.modifiers == hotkey.modifiers) {
        hasOtherSameHotkey = true;
        break;
      }
    }

    // Only ungrab if no other enabled hotkeys use this key+modifier combo
    if (!hasOtherSameHotkey) {
      Ungrab(hotkey.key, hotkey.modifiers, root, false);
      info("Physically ungrabbed key {} with modifiers 0x{:x}", hotkey.key,
           hotkey.modifiers);
    } else {
      info("Not ungrabbing - other hotkeys still using key {} with modifiers "
           "0x{:x}",
           hotkey.key, hotkey.modifiers);
    }
  }

  // Always disable the hotkey entry
  hotkeys[hotkeyId].enabled = false;

  info("Successfully ungrabbed hotkey: {}", hotkey.alias);
  return true;
#else
  return false;
#endif
}

bool IO::GrabHotkeysByPrefix(const std::string &prefix) {
#ifdef __linux__
  if (!display)
    return false;

  bool success = true;
  for (const auto &[id, hotkey] : hotkeys) {
    if (hotkey.alias.find(prefix) == 0) {
      if (!GrabHotkey(id)) {
        success = false;
      }
    }
  }
  return success;
#else
  return false;
#endif
}

bool IO::UngrabHotkeysByPrefix(const std::string &prefix) {
#ifdef __linux__
  if (!display)
    return false;

  bool success = true;
  for (const auto &[id, hotkey] : hotkeys) {
    if (hotkey.alias.find(prefix) == 0) {
      if (!UngrabHotkey(id)) {
        success = false;
      }
    }
  }
  return success;
#else
  return false;
#endif
}

void IO::Map(const std::string &from, const std::string &to) {
  KeySym fromKey = StringToVirtualKey(from);
  KeySym toKey = StringToVirtualKey(to);
  if (fromKey != NoSymbol && toKey != NoSymbol) {
    keyMapInternal[fromKey] = toKey;
  }
}

void IO::Remap(const std::string &key1, const std::string &key2) {
  KeySym k1 = StringToVirtualKey(key1);
  KeySym k2 = StringToVirtualKey(key2);
  if (k1 != NoSymbol && k2 != NoSymbol) {
    remappedKeys[k1] = k2;
    remappedKeys[k2] = k1;
  }
}
bool IO::MatchEvdevModifiers(int expectedModifiers,
                             const std::map<int, bool> &keyState) {
  auto isPressed = [&](int key) -> bool {
    auto it = keyState.find(key);
    return it != keyState.end() && it->second;
  };

  // Check each modifier type
  std::vector<std::pair<int, std::pair<int, int>>> modifiers = {
      {KEY_LEFTCTRL, {KEY_LEFTCTRL, KEY_RIGHTCTRL}},
      {KEY_LEFTSHIFT, {KEY_LEFTSHIFT, KEY_RIGHTSHIFT}},
      {KEY_LEFTALT, {KEY_LEFTALT, KEY_RIGHTALT}},
      {KEY_LEFTMETA, {KEY_LEFTMETA, KEY_RIGHTMETA}}};

  for (auto &[flag, keys] : modifiers) {
    bool expected = (expectedModifiers & flag) != 0;
    bool pressed = isPressed(keys.first) || isPressed(keys.second);

    if (expected != pressed)
      return false;
  }

  return true;
}
bool IO::StartEvdevHotkeyListener(const std::string &devicePath) {
  if (evdevRunning)
    return false;
  evdevDevicePath = devicePath;
  evdevRunning = true;

  evdevThread = std::thread([this]() {
    int fd = open(evdevDevicePath.c_str(), O_RDONLY | O_NONBLOCK);
    if (fd < 0) {
      error("evdev: cannot open " + evdevDevicePath + ": " +
            std::string(strerror(errno)) + "\n");
      evdevRunning = false;
      return;
    }
    if (ioctl(fd, EVIOCGRAB, 1) < 0) {
      error("evdev: failed to grab device exclusively: {}", strerror(errno));
      // Continue without exclusive grab - still works for monitoring
    } else {
      info("evdev: grabbed device exclusively");
    }
    if (!SetupUinputDevice()) {
      close(fd);
      evdevRunning = false;
      error("evdev: failed to setup uinput device\n");
      return;
    }

    struct input_event ev{};
    std::map<int, bool> modState;

    while (evdevRunning) {
      ssize_t n = read(fd, &ev, sizeof(ev));
      if (n != sizeof(ev)) {
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
        continue;
      }

      if (ev.type != EV_KEY)
        continue;

      bool down = (ev.value == 1 || ev.value == 2);
      int code = ev.code;
      evdevKeyState[code] = down;

      // Update modifier state
      modState[ControlMask] =
          evdevKeyState[KEY_LEFTCTRL] || evdevKeyState[KEY_RIGHTCTRL];
      modState[ShiftMask] =
          evdevKeyState[KEY_LEFTSHIFT] || evdevKeyState[KEY_RIGHTSHIFT];
      modState[Mod1Mask] =
          evdevKeyState[KEY_LEFTALT] || evdevKeyState[KEY_RIGHTALT];
      modState[Mod4Mask] =
          evdevKeyState[KEY_LEFTMETA] || evdevKeyState[KEY_RIGHTMETA];

      keyDownState[code] = down;

      // Process hotkeys
      std::vector<std::function<void()>> callbacks;
      bool shouldBlockKey = false;

      {
        std::scoped_lock hotkeyLock(hotkeyMutex);
        for (auto &[id, hotkey] : hotkeys) {
          if (!hotkey.enabled || !hotkey.evdev ||
              hotkey.key != static_cast<Key>(code))
            continue;

          // Event type check
          if (hotkey.eventType == HotkeyEventType::Down && !down)
            continue;
          if (hotkey.eventType == HotkeyEventType::Up && down)
            continue;

          // Modifier matching
          bool isModifierKey =
              (code == KEY_LEFTALT || code == KEY_RIGHTALT ||
               code == KEY_LEFTCTRL || code == KEY_RIGHTCTRL ||
               code == KEY_LEFTSHIFT || code == KEY_RIGHTSHIFT ||
               code == KEY_LEFTMETA || code == KEY_RIGHTMETA);

          bool modifierMatch;
          if (isModifierKey && hotkey.modifiers == 0) {
            modifierMatch = true;
          } else {
            bool ctrlRequired = (hotkey.modifiers & (1 << 0)) != 0;
            bool shiftRequired = (hotkey.modifiers & (1 << 1)) != 0;
            bool altRequired = (hotkey.modifiers & (1 << 2)) != 0;
            bool metaRequired = (hotkey.modifiers & (1 << 3)) != 0;

            bool ctrlPressed = modState[ControlMask];
            bool shiftPressed = modState[ShiftMask];
            bool altPressed = modState[Mod1Mask];
            bool metaPressed = modState[Mod4Mask];

            modifierMatch = (ctrlRequired == ctrlPressed) &&
                            (shiftRequired == shiftPressed) &&
                            (altRequired == altPressed) &&
                            (metaRequired == metaPressed);
          }

          if (!modifierMatch)
            continue;

          // Context checks
          if (!hotkey.contexts.empty()) {
            if (!std::all_of(hotkey.contexts.begin(), hotkey.contexts.end(),
                             [](auto &ctx) { return ctx(); })) {
              continue;
            }
          }
          hotkey.success = true;

          // Add callback if it exists
          if (hotkey.callback) {
            callbacks.push_back(hotkey.callback);
          }
          if (hotkey.grab) {
            shouldBlockKey = true;
          }
        }
      }

      // Execute callbacks
      for (auto &callback : callbacks) {
        try {
          callback();
        } catch (const std::exception &e) {
          error("Error in hotkey callback: {}", e.what());
        }
      }
      if (shouldBlockKey) {
        debug("Blocking key {} ({}) - hotkey requested blocking", code,
              down ? "down" : "up");
      } else {
        // Pass key through to system
        SendUInput(code, down);
      }
    }

    close(fd);
  });

  return true;
}

void IO::StopEvdevHotkeyListener() {
  if (!evdevRunning)
    return;
  evdevRunning = false;
  if (evdevThread.joinable())
    evdevThread.join();
  {
    std::scoped_lock lock(blockedKeysMutex);
    blockedKeys.clear();
  }

  CleanupUinputDevice();
}

void IO::CleanupUinputDevice() {
  StopEvdevMouseListener();

  if (mouseUinputFd >= 0) {
    ioctl(mouseUinputFd, UI_DEV_DESTROY);
    close(mouseUinputFd);
    mouseUinputFd = -1;
  }
  if (uinputFd >= 0) {
    ioctl(uinputFd, UI_DEV_DESTROY);
    close(uinputFd);
    uinputFd = -1;
  }
}
std::string IO::findEvdevDevice(const std::string &deviceName) {
  std::ifstream proc("/proc/bus/input/devices");
  std::string line;
  std::string currentName;

  while (std::getline(proc, line)) {
    if (line.starts_with("N: Name=")) {
      // Extract device name: N: Name="INSTANT USB Keyboard"
      currentName = line.substr(8);
      if (!currentName.empty() && currentName[0] == '"') {
        currentName = currentName.substr(1, currentName.length() - 2);
      }
    } else if (line.starts_with("H: Handlers=") && currentName == deviceName) {
      // Look for "kbd" handler to ensure it's a real keyboard
      if (line.find("kbd") != std::string::npos) {
        // Extract event number: H: Handlers=sysrq kbd leds event7
        size_t eventPos = line.find("event");
        if (eventPos != std::string::npos) {
          std::string eventStr = line.substr(eventPos + 5);
          size_t spacePos = eventStr.find(' ');
          if (spacePos != std::string::npos) {
            eventStr = eventStr.substr(0, spacePos);
          }
          return "/dev/input/event" + eventStr;
        }
      }
    }

    return "";
  }
}

std::string IO::detectEvdevDevice(
    const std::vector<std::string> &patterns,
    const std::function<bool(const std::string &)> &typeFilter) {
  // Get all input devices
  std::vector<InputDevice> devices = getInputDevices();

  // Debug: Log all found devices
  info("Scanning {} input devices for matches...", devices.size());
  for (const auto &dev : devices) {
    debug("  Device: {} (type: {}, id: {})", dev.name, dev.type, dev.id);
  }

  // Try to match devices in order of preference
  for (const auto &pattern : patterns) {
    debug("Trying pattern: '{}'", pattern);

    for (const auto &inputDevice : devices) {
      // Check if pattern matches device name and type matches filter
      if (inputDevice.name.find(pattern) != std::string::npos &&
          typeFilter(inputDevice.type) && !inputDevice.evdevPath.empty()) {

        info("Matched device: '{}' (type: {}, evdev: {}) with pattern: '{}'",
             inputDevice.name, inputDevice.type, inputDevice.evdevPath,
             pattern);
        return inputDevice.evdevPath;
      }
    }
  }

  return "";
}

std::string IO::getKeyboardDevice() {
  // Common keyboard device name patterns (in order of preference)
  std::vector<std::string> keyboardPatterns = {
      "keyboard",  "usb",        "bluetooth", "bt",          "hid",
      "logitech",  "razer",      "corsair",   "steelseries", "apple",
      "microsoft", "dell",       "hp",        "asus",        "wireless",
      "gaming",    "mechanical", "membrane",  "translated",  "boot"};

  auto isKeyboard = [](const std::string &type) {
    return type.find("keyboard") != std::string::npos ||
           type.find("key") != std::string::npos;
  };

  return detectEvdevDevice(keyboardPatterns, isKeyboard);
}

std::string IO::getMouseDevice() {
  // Common mouse device name patterns (in order of preference)
  std::vector<std::string> mousePatterns = {
      "mouse",     "trackpad", "trackball", "touchpad",    "usb",
      "bluetooth", "logitech", "razer",     "steelseries", "microsoft",
      "dell",      "hp",       "asus",      "wireless",    "gaming"};

  auto isMouse = [](const std::string &type) {
    return type.find("mouse") != std::string::npos ||
           type.find("pointer") != std::string::npos ||
           type.find("touchpad") != std::string::npos ||
           type.find("trackpad") != std::string::npos;
  };

  return detectEvdevDevice(mousePatterns, isMouse);
}

std::vector<InputDevice> IO::getInputDevices() {
  std::vector<InputDevice> devices;

  Display *display = DisplayManager::GetDisplay();
  if (!display) {
    error("Cannot open display for device enumeration");
    return devices;
  }

  int xi_opcode, event, error_code;
  if (!XQueryExtension(display, "XInputExtension", &xi_opcode, &event,
                       &error_code)) {
    error("X Input extension not available");
    return devices;
  }

  // Check XInput2 version
  int major = 2, minor = 0;
  if (XIQueryVersion(display, &major, &minor) == x11::XBadRequest) {
    error("XInput2 not available");
    return devices;
  }

  int ndevices;
  XIDeviceInfo *info = XIQueryDevice(display, XIAllDevices, &ndevices);

  for (int i = 0; i < ndevices; i++) {
    InputDevice device;
    device.id = info[i].deviceid;
    device.name = info[i].name;
    device.enabled = info[i].enabled;

    // Determine device type
    switch (info[i].use) {
    case XIMasterPointer:
      device.type = "master pointer";
      break;
    case XIMasterKeyboard:
      device.type = "master keyboard";
      break;
    case XISlavePointer:
      device.type = "slave pointer";
      break;
    case XISlaveKeyboard:
      device.type = "slave keyboard";
      break;
    case XIFloatingSlave:
      device.type = "floating slave";
      break;
    default:
      device.type = "unknown";
    }

    // Find the real evdev path for keyboards
    if (device.type == "slave keyboard" || device.type == "master keyboard") {
      std::string evdevPath = findEvdevDevice(device.name);
      if (!evdevPath.empty()) {
        device.evdevPath = evdevPath;
        debug("Device '{}' (XInput ID: {}) maps to {}", device.name, device.id,
              evdevPath);
      }
    }

    devices.push_back(device);
  }

  XIFreeDeviceInfo(info);
  return devices;
}
void IO::listInputDevices() {
  auto devices = getInputDevices();

  std::cout << "⎡ Virtual core pointer                    id=" << std::endl;
  std::cout << "⎜   ↳ Virtual core XTEST pointer          id=" << std::endl;

  for (const auto &device : devices) {
    std::string prefix = "⎜   ↳ ";
    if (device.type == "master pointer" || device.type == "master keyboard") {
      prefix = "⎡ ";
    }

    std::cout << prefix << device.name << "    id=" << device.id << "    ["
              << device.type << " ("
              << (device.enabled ? "enabled" : "disabled") << ")]" << std::endl;
  }
}

// Mouse event handling methods
bool IO::StartEvdevMouseListener(const std::string &mouseDevicePath) {
  if (mouseEvdevRunning)
    return false;

  mouseEvdevDevicePath = mouseDevicePath;
  mouseEvdevRunning = true;

  mouseEvdevThread = std::thread([this]() {
    int fd = open(mouseEvdevDevicePath.c_str(), O_RDONLY | O_NONBLOCK);
    if (fd < 0) {
      error("mouse evdev: cannot open {}: {}", mouseEvdevDevicePath,
            strerror(errno));
      mouseEvdevRunning = false;
      return;
    }

    // Grab mouse exclusively
    if (ioctl(fd, EVIOCGRAB, 1) < 0) {
      warning("mouse evdev: failed to grab exclusively: {}", strerror(errno));
      // Continue anyway - we can still monitor
    } else {
      info("mouse evdev: grabbed mouse exclusively");
    }

    if (!SetupMouseUinputDevice()) {
      close(fd);
      mouseEvdevRunning = false;
      error("mouse evdev: failed to setup uinput device");
      return;
    }

    struct input_event ev{};
    bool leftPressed = false;
    bool rightPressed = false;
    bool altPressed = getGlobalAltState();

    while (mouseEvdevRunning) {
      ssize_t n = read(fd, &ev, sizeof(ev));
      if (n != sizeof(ev)) {
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
        continue;
      }

      bool shouldBlock = false;

      // Handle different mouse events
      switch (ev.type) {
      case EV_KEY: // Mouse buttons
        shouldBlock =
            handleMouseButton(ev, leftPressed, rightPressed, altPressed);
        break;

      case EV_REL: // Mouse movement and scroll
        shouldBlock = handleMouseRelative(ev, altPressed);
        break;

      case EV_ABS: // Absolute positioning (touchpads)
        shouldBlock = handleMouseAbsolute(ev, altPressed);
        break;
      }

      // Only pass through if not blocked
      if (!shouldBlock) {
        SendMouseUInput(ev);
      }
    }

    close(fd);
  });

  return true;
}

void IO::StopEvdevMouseListener() {
  if (!mouseEvdevRunning)
    return;

  mouseEvdevRunning = false;

  if (mouseEvdevThread.joinable()) {
    mouseEvdevThread.join();
  }
}

bool IO::handleMouseButton(const input_event &ev, bool &leftPressed,
                           bool &rightPressed, bool altPressed) {
  bool shouldBlock = false;
  auto now = std::chrono::steady_clock::now();

  // Get current modifier state
  int currentModifiers = 0;
  if (IsCtrlPressed())
    currentModifiers |= ControlMask;
  if (IsShiftPressed())
    currentModifiers |= ShiftMask;
  if (altPressed)
    currentModifiers |= Mod1Mask;
  if (IsWinPressed())
    currentModifiers |= Mod4Mask;

  // Check for registered hotkeys
  for (auto &[id, hotkey] : hotkeys) {
    if (!hotkey.enabled || hotkey.type != HotkeyType::MouseButton)
      continue;

    // Check if this is the right button and event type
    bool isButtonMatch = false;
    switch (ev.code) {
    case BTN_LEFT:
      isButtonMatch = (hotkey.mouseButton == BTN_LEFT);
      break;
    case BTN_RIGHT:
      isButtonMatch = (hotkey.mouseButton == BTN_RIGHT);
      break;
    case BTN_MIDDLE:
      isButtonMatch = (hotkey.mouseButton == BTN_MIDDLE);
      break;
    case BTN_SIDE:
      isButtonMatch = (hotkey.mouseButton == BTN_SIDE);
      break;
    case BTN_EXTRA:
      isButtonMatch = (hotkey.mouseButton == BTN_EXTRA);
      break;
    }

    if (isButtonMatch &&
        (hotkey.eventType == HotkeyEventType::Both ||
         (hotkey.eventType == HotkeyEventType::Down && ev.value == 1) ||
         (hotkey.eventType == HotkeyEventType::Up && ev.value == 0))) {

      // Check modifiers match
      if ((hotkey.modifiers & currentModifiers) == hotkey.modifiers) {
        // Execute the hotkey callback
        if (hotkey.callback) {
          hotkey.callback();
        }
        shouldBlock = hotkey.grab;

        // If this is a blocking hotkey, we're done
        if (shouldBlock) {
          return true;
        }
      }
    }
  }

  // Legacy combo detection (keep for backward compatibility)
  switch (ev.code) {
  case BTN_LEFT:
    leftPressed = (ev.value == 1);
    if (leftPressed) {
      lastLeftPress = now;
    }
    if (leftPressed && rightPressed) {
      auto timeDiff = std::chrono::duration_cast<std::chrono::milliseconds>(
                          now - lastRightPress)
                          .count();
      if (timeDiff < 500) {
        info("🎯 LEFT+RIGHT COMBO DETECTED! ({}ms)", timeDiff);
        executeComboAction("left_right_combo");
        shouldBlock = true;
      }
    }
    break;
  case BTN_RIGHT:
    rightPressed = (ev.value == 1);
    if (rightPressed) {
      lastRightPress = now;
      if (leftPressed) {
        auto timeDiff = std::chrono::duration_cast<std::chrono::milliseconds>(
                            now - lastLeftPress)
                            .count();
        if (timeDiff < 500) {
          info("🎯 RIGHT+LEFT COMBO DETECTED! ({}ms)", timeDiff);
          executeComboAction("right_left_combo");
          shouldBlock = true;
        }
      }
    }
    break;
  case BTN_MIDDLE:
    if (ev.value == 1 && altPressed) {
      info("🎯 ALT+MIDDLE CLICK!");
      executeComboAction("alt_middle_click");
      shouldBlock = true;
    }
    break;
  }
  return shouldBlock;
}

bool IO::handleMouseRelative(const input_event &ev, bool altPressed) {
  bool shouldBlock = false;

  // Get current modifier state
  int currentModifiers = 0;
  if (IsCtrlPressed())
    currentModifiers |= ControlMask;
  if (IsShiftPressed())
    currentModifiers |= ShiftMask;
  if (altPressed)
    currentModifiers |= Mod1Mask;
  if (IsWinPressed())
    currentModifiers |= Mod4Mask;

  // Check for wheel events first
  if (ev.code == REL_WHEEL || ev.code == REL_HWHEEL) {
    // Check for registered wheel hotkeys
    for (auto &[id, hotkey] : hotkeys) {
      if (!hotkey.enabled || hotkey.type != HotkeyType::MouseWheel)
        continue;

      // Check if this is the right wheel direction
      bool isWheelMatch = false;
      if (ev.code == REL_WHEEL) { // Vertical wheel
        isWheelMatch = (hotkey.wheelDirection == (ev.value > 0 ? 1 : -1));
      } else if (ev.code == REL_HWHEEL) { // Horizontal wheel
        isWheelMatch = (hotkey.wheelDirection == (ev.value > 0 ? 1 : -1));
      }

      if (isWheelMatch) {
        // Check if modifiers match (exact match required)
        if (hotkey.modifiers == currentModifiers) {
          // Execute the hotkey callback
          if (hotkey.callback) {
            hotkey.callback();
          }
          shouldBlock = hotkey.grab;

          // If this is a blocking hotkey, we're done
          if (shouldBlock) {
            return true;
          }
        }
      }
    }
  }

  // Legacy behavior
  switch (ev.code) {
  case REL_WHEEL: // Vertical scroll
    if (altPressed) {
      info("🎯 ALT+SCROLL WHEEL: {}", ev.value > 0 ? "UP" : "DOWN");

      if (ev.value > 0) {
        executeComboAction("alt_scroll_up");
      } else {
        executeComboAction("alt_scroll_down");
      }

      shouldBlock = true; // Block the scroll from reaching system
    }
    break;

  case REL_HWHEEL: // Horizontal scroll
    if (altPressed) {
      info("🎯 ALT+HORIZONTAL SCROLL: {}", ev.value > 0 ? "RIGHT" : "LEFT");
      executeComboAction("alt_hscroll");
      shouldBlock = true;
    }
    break;

  case REL_X: // Mouse movement X
  case REL_Y: // Mouse movement Y
    // You could detect alt+drag here
    if (altPressed && (abs(ev.value) > 5)) { // Significant movement
      // executeComboAction("alt_drag");
      // shouldBlock = true; // Uncomment to block mouse movement with alt
    }
    break;
  }

  return shouldBlock;
}

bool IO::handleMouseAbsolute(const input_event &ev, bool altPressed) {
  // Get current modifier state
  int currentModifiers = 0;
  if (IsCtrlPressed())
    currentModifiers |= ControlMask;
  if (IsShiftPressed())
    currentModifiers |= ShiftMask;
  if (altPressed)
    currentModifiers |= Mod1Mask;
  if (IsWinPressed())
    currentModifiers |= Mod4Mask;

  // Check for registered hotkeys that might be interested in absolute events
  for (auto &[id, hotkey] : hotkeys) {
    if (!hotkey.enabled || hotkey.type != HotkeyType::MouseMove)
      continue;

    // Check if this hotkey is interested in this specific absolute axis
    bool isAxisMatch = false;
    switch (ev.code) {
    case ABS_X:
    case ABS_Y:                                // Position
      isAxisMatch = (hotkey.mouseButton == 0); // 0 means any position
      break;
    case ABS_PRESSURE:                          // Pressure
      isAxisMatch = (hotkey.mouseButton == -2); // -2 means pressure
      break;
    case ABS_DISTANCE:                          // Distance
      isAxisMatch = (hotkey.mouseButton == -3); // -3 means distance
      break;
    }

    if (isAxisMatch) {
      // Check if modifiers match (exact match required)
      if ((hotkey.modifiers & currentModifiers) == hotkey.modifiers) {
        // Execute the hotkey callback with the current value
        if (hotkey.callback) {
          hotkey.callback();
        }

        // If this is a blocking hotkey, we're done
        if (hotkey.grab) {
          return true;
        }
      }
    }
  }

  // Currently not blocking any absolute events by default
  return false;
}

bool IO::SetupMouseUinputDevice() {
  mouseUinputFd = open("/dev/uinput", O_WRONLY | O_NONBLOCK);
  if (mouseUinputFd < 0) {
    error("mouse uinput: failed to open /dev/uinput: {}", strerror(errno));
    return false;
  }

  struct uinput_setup usetup = {};
  usetup.id.bustype = BUS_USB;
  usetup.id.vendor = 0x1234;
  usetup.id.product = 0x5679;
  strcpy(usetup.name, "havel-uinput-mouse");

  // Enable mouse events
  ioctl(mouseUinputFd, UI_SET_EVBIT, EV_KEY);
  ioctl(mouseUinputFd, UI_SET_EVBIT, EV_REL);
  ioctl(mouseUinputFd, UI_SET_EVBIT, EV_ABS);
  ioctl(mouseUinputFd, UI_SET_EVBIT, EV_SYN);

  // Enable mouse buttons
  ioctl(mouseUinputFd, UI_SET_KEYBIT, BTN_LEFT);
  ioctl(mouseUinputFd, UI_SET_KEYBIT, BTN_RIGHT);
  ioctl(mouseUinputFd, UI_SET_KEYBIT, BTN_MIDDLE);
  ioctl(mouseUinputFd, UI_SET_KEYBIT, BTN_SIDE);
  ioctl(mouseUinputFd, UI_SET_KEYBIT, BTN_EXTRA);

  // Enable mouse movement and scroll
  ioctl(mouseUinputFd, UI_SET_RELBIT, REL_X);
  ioctl(mouseUinputFd, UI_SET_RELBIT, REL_Y);
  ioctl(mouseUinputFd, UI_SET_RELBIT, REL_WHEEL);
  ioctl(mouseUinputFd, UI_SET_RELBIT, REL_HWHEEL);

  // Enable absolute positioning (for touchpads)
  ioctl(mouseUinputFd, UI_SET_ABSBIT, ABS_X);
  ioctl(mouseUinputFd, UI_SET_ABSBIT, ABS_Y);

  if (ioctl(mouseUinputFd, UI_DEV_SETUP, &usetup) < 0) {
    error("mouse uinput: device setup failed: {}", strerror(errno));
    close(mouseUinputFd);
    mouseUinputFd = -1;
    return false;
  }

  if (ioctl(mouseUinputFd, UI_DEV_CREATE) < 0) {
    error("mouse uinput: device creation failed: {}", strerror(errno));
    close(mouseUinputFd);
    mouseUinputFd = -1;
    return false;
  }

  std::this_thread::sleep_for(std::chrono::milliseconds(100));
  return true;
}

void IO::SendMouseUInput(const input_event &ev) {
  if (mouseUinputFd < 0)
    return;

  struct input_event uievent = ev;
  write(mouseUinputFd, &uievent, sizeof(uievent));

  // Sync after each event
  struct input_event syn = {};
  syn.type = EV_SYN;
  syn.code = SYN_REPORT;
  syn.value = 0;
  write(mouseUinputFd, &syn, sizeof(syn));
}

void IO::setGlobalAltState(bool pressed) { globalAltPressed.store(pressed); }

bool IO::getGlobalAltState() { return globalAltPressed.load(); }

void IO::executeComboAction(const std::string &action) {
  // Find and execute the matching hotkey
  for (auto &[id, hotkey] : hotkeys) {
    if (hotkey.action == action && hotkey.callback) {
      hotkey.callback();
      return;
    }
  }
  info("No handler registered for combo action: {}", action);
}

// Mouse button click methods
void IO::MouseClick(int button) {
  MouseDown(button);
  std::this_thread::sleep_for(std::chrono::milliseconds(10)); // Small delay
  MouseUp(button);
}

void IO::MouseDown(int button) {
  struct input_event ev = {};
  ev.type = EV_KEY;
  ev.code = button;
  ev.value = 1; // Press

  if (mouseUinputFd >= 0) {
    write(mouseUinputFd, &ev, sizeof(ev));

    // Sync
    struct input_event syn = {};
    syn.type = EV_SYN;
    syn.code = SYN_REPORT;
    syn.value = 0;
    write(mouseUinputFd, &syn, sizeof(syn));
  }
}

void IO::MouseUp(int button) {
  struct input_event ev = {};
  ev.type = EV_KEY;
  ev.code = button;
  ev.value = 0; // Release

  if (mouseUinputFd >= 0) {
    write(mouseUinputFd, &ev, sizeof(ev));

    // Sync
    struct input_event syn = {};
    syn.type = EV_SYN;
    syn.code = SYN_REPORT;
    syn.value = 0;
    write(mouseUinputFd, &syn, sizeof(syn));
  }
}

void IO::MouseWheel(int amount) {
  struct input_event ev = {};
  ev.type = EV_REL;
  ev.code = REL_WHEEL;
  ev.value = amount > 0 ? 1 : -1;

  if (mouseUinputFd >= 0) {
    write(mouseUinputFd, &ev, sizeof(ev));

    // Sync
    struct input_event syn = {};
    syn.type = EV_SYN;
    syn.code = SYN_REPORT;
    syn.value = 0;
    write(mouseUinputFd, &syn, sizeof(syn));
  }
}
} // namespace havel
