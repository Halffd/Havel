#include "IO.hpp"
#include "../utils/Logger.hpp"
#include "../utils/Utils.hpp"
#include "../window/WindowManager.hpp"
#include "./ConfigManager.hpp"
#include "core/DisplayManager.hpp"
#include "x11.h"
#include <algorithm>
#include <cmath>
#include <iostream>
#include <thread>

namespace havel {
#if defined(WINDOWS)
HHOOK IO::keyboardHook = NULL;
#endif
std::unordered_map<int, HotKey> IO::hotkeys; // Map to store hotkeys by ID
bool IO::hotkeyEnabled = true;
int IO::hotkeyCount = 0;

int xerrorHandler(Display *display, XErrorEvent *error) {
  char errorText[256];
  XGetErrorText(display, error->error_code, errorText, sizeof(errorText));

  std::cerr << "X Error: " << errorText << " (" << (int)error->error_code << ")"
            << std::endl;
  std::cerr << "  Request code: " << (int)error->request_code << std::endl;
  std::cerr << "  Minor code: " << (int)error->minor_code << std::endl;
  std::cerr << "  Resource ID: " << error->resourceid << std::endl;

  return 0; // Must return a value
}

IO::IO() {
  std::cout << "IO constructor called" << std::endl;

  // Set the error handler before making your XGrabKey call
  XSetErrorHandler(xerrorHandler);
  DisplayManager::Initialize();
  display = DisplayManager::GetDisplay();
  if (!display) {
    std::cerr << "Failed to get X11 display" << std::endl;
  }
  InitKeyMap();

  // Start hotkey monitoring thread for X11
#ifdef __linux__
  if (display) {
    // Set up X11 error handler
    XSetErrorHandler([](Display *, XErrorEvent *) -> int { return 0; });

    // Start hotkey monitoring in a separate thread
    timerRunning = true;
    timerThread = std::thread([this]() { this->MonitorHotkeys(); });
  }
  StartEvdevHotkeyListener("/dev/input/event7"); // your keyboard device
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
  strcpy(usetup.name, "wusper-uinput-kb");
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

void IO::Grab(Key input, unsigned int modifiers, Window root, bool exclusive,
              bool isMouse) {
  if (!display)
    return;

  bool isButton = isMouse ? isMouse : (input >= Button1 && input <= 7);
  // simple check

  // Modifier variants (add NumLock/CapsLock later if needed)
  unsigned int modVariants[] = {0 /*, LockMask, numlockMask, etc. */};

  for (unsigned int variant : modVariants) {
    unsigned int finalMods = modifiers | variant;

    if (isButton) {
      // Mouse button grab
      XUngrabButton(display, input, finalMods, root);

      if (exclusive) {
        XGrabButton(display, input, finalMods, root, x11::XTrue,
                    ButtonPressMask | ButtonReleaseMask, GrabModeAsync,
                    GrabModeAsync, x11::XNone, x11::XNone);
      } else {
        XSelectInput(display, root, ButtonPressMask | ButtonReleaseMask);
      }
    } else {
      // Keyboard key grab
      XUngrabKey(display, input, finalMods, root);

      if (exclusive) {
        x11::XStatus status = XGrabKey(display, input, finalMods, root, x11::XTrue,
                                 GrabModeAsync, GrabModeAsync);
        if (status != x11::XSuccess) {
          error(std::string("Failed to grab key (code: ") + std::to_string((int)input) + ") with modifiers: " + std::to_string(finalMods));
          failedHotkeys.push_back({
            .key = input,
            .modifiers = static_cast<int>(finalMods),
            .enabled = false,
            .blockInput = exclusive,
            .suspend = false,
            .exclusive = exclusive,
            .success = false,
            .evdev = false,
            .eventType = HotkeyEventType::Down,
          });
        }
      } else {
        XSelectInput(display, root, x11::XKeyPressMask | x11::XKeyReleaseMask);
      }
    }
  }

  XSync(display, x11::XFalse);
}

void crash() {
  int *ptr = nullptr;
  *ptr = 0;
}

void IO::Ungrab(Key input, unsigned int modifiers, Window root) {
  if (!display)
    return;

  bool isButton = (input >= Button1 && input <= 7);

  XUngrabKey(display, input, modifiers, root);
  XUngrabKey(display, input, modifiers | LockMask, root);

  if (isButton) {
    XUngrabButton(display, input, modifiers, root);
    XUngrabButton(display, input, modifiers | LockMask, root);
  }

  XSync(display, x11::XFalse);
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
          return ks == XK_Shift_L || ks == XK_Shift_R || 
                 ks == XK_Control_L || ks == XK_Control_R || 
                 ks == XK_Alt_L || ks == XK_Alt_R ||
                 ks == XK_Meta_L || ks == XK_Meta_R || 
                 ks == XK_Super_L || ks == XK_Super_R || 
                 ks == XK_Hyper_L || ks == XK_Hyper_R ||
                 ks == XK_Caps_Lock || ks == XK_Shift_Lock || 
                 ks == XK_Num_Lock || ks == XK_Scroll_Lock;
      };
      
      // Pre-allocate containers
      std::vector<std::function<void()>> callbacks;
      callbacks.reserve(16);
      
      // Error message constants
      static const std::string callback_error_prefix = "Error in hotkey callback: ";
      static const std::string event_error_prefix = "Error processing X11 event: ";
      
      constexpr unsigned int relevantModifiers = ShiftMask | LockMask | ControlMask | 
                                               Mod1Mask | Mod4Mask | Mod5Mask;
      
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
                      if (event.type != KeyPress && event.type != KeyRelease) {
                          continue;
                      }
                      
                      const bool isDown = (event.type == KeyPress);
                      const XKeyEvent* keyEvent = &event.xkey;
                      
                      KeySym keysym;
                      {
                          std::lock_guard<std::mutex> lock(x11Mutex);
                          if (!display) break;
                          keysym = XLookupKeysym(const_cast<XKeyEvent*>(keyEvent), 0);
                      }
                      
                      if (keysym == NoSymbol) {
                          continue;
                      }
                      
                      // Handle remapped keys
                      if (const auto remappedIt = remappedKeys.find(keysym); 
                          remappedIt != remappedKeys.end() && remappedIt->second != NoSymbol) {
                          
                          const char* keySymStr;
                          {
                              std::lock_guard<std::mutex> lock(x11Mutex);
                              if (!display) break;
                              keySymStr = XKeysymToString(remappedIt->second);
                          }
                          
                          if (keySymStr) {
                              if (const int keycode = EvdevNameToKeyCode(keySymStr); keycode != -1) {
                                  SendUInput(keycode, isDown);
                              }
                          }
                          continue; // Suppress original event
                      }
  
                      // Handle mapped keys
                      if (const auto mappedIt = keyMapInternal.find(keysym); 
                          mappedIt != keyMapInternal.end()) {
                          
                          const char* keySymStr;
                          {
                              std::lock_guard<std::mutex> lock(x11Mutex);
                              if (!display) break;
                              keySymStr = XKeysymToString(mappedIt->second);
                          }
                          
                          if (keySymStr) {
                              if (const int keycode = EvdevNameToKeyCode(keySymStr); keycode != -1) {
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
                              if (const char* keysym_str = XKeysymToString(keysym)) {
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
                          for (const auto& [id, hotkey] : hotkeys) {
                              if (!hotkey.enabled) continue;
                              
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
                                        " vs expected: " + std::to_string(hotkey.modifiers) + ")");
                                  #endif
                                  
                                  if (hotkey.callback) {
                                      callbacks.emplace_back(hotkey.callback);
                                  }
                                  break; // Only one hotkey should match per event
                              }
                          }
                      }
                      
                      // Execute callbacks outside all locks
                      for (const auto& callback : callbacks) {
                          try {
                              callback();
                          } catch (const std::exception& e) {
                              error(callback_error_prefix + e.what());
                          } catch (...) {
                              error("Unknown error in hotkey callback");
                          }
                      }
                      
                  } catch (const std::exception& e) {
                      error(event_error_prefix + e.what());
                  } catch (...) {
                      error("Unknown error processing X11 event");
                  }
              }
          }
      } catch (const std::exception& e) {
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
    if (write(uinputFd, &ev, sizeof(ev)) < 0) return false;
    
    ev.type = EV_SYN;
    ev.code = SYN_REPORT;
    ev.value = 0;
    if (write(uinputFd, &ev, sizeof(ev)) < 0) return false;
    
    return true;
  } 
  else if (action == 0) { // Release
    ev.type = EV_KEY;
    ev.code = btnCode;
    ev.value = 0; // Release
    if (write(uinputFd, &ev, sizeof(ev)) < 0) return false;
    
    ev.type = EV_SYN;
    ev.code = SYN_REPORT;
    ev.value = 0;
    if (write(uinputFd, &ev, sizeof(ev)) < 0) return false;
    
    return true;
  }
  else if (action == 2) { // Click (press and release)
    // Press
    ev.type = EV_KEY;
    ev.code = btnCode;
    ev.value = 1;
    if (write(uinputFd, &ev, sizeof(ev)) < 0) return false;
    
    // Sync
    ev.type = EV_SYN;
    ev.code = SYN_REPORT;
    ev.value = 0;
    if (write(uinputFd, &ev, sizeof(ev)) < 0) return false;
    
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

bool IO::MouseMove(int dx, int dy, int speed = 1, float accel = 1.0f) {
    static std::mutex uinputMutex;
    std::lock_guard<std::mutex> lock(uinputMutex);
  if (speed <= 0)
    speed = 1;
  if (accel <= 0.0f)
    accel = 1.0f;

  int steps = std::max(abs(dx), abs(dy)) / speed;
  steps = std::max(steps, 1);

  float stepx = dx / static_cast<float>(steps);
  float stepy = dy / static_cast<float>(steps);

  for (int i = 0; i < steps; ++i) {
    input_event ev = {};
    ev.type = EV_REL;

    ev.code = REL_X;
    ev.value = static_cast<int>(roundf(stepx));
    if (write(uinputFd, &ev, sizeof(ev)) < 0)
      return false;

    ev.code = REL_Y;
    ev.value = static_cast<int>(roundf(stepy));
    if (write(uinputFd, &ev, sizeof(ev)) < 0)
      return false;

    ev.type = EV_SYN;
    ev.code = SYN_REPORT;
    ev.value = 0;
    if (write(uinputFd, &ev, sizeof(ev)) < 0)
      return false;

    std::this_thread::sleep_for(
        std::chrono::microseconds(static_cast<int>(1000 * accel)));
  }

  return true;
}
bool IO::Scroll(int dy, int dx) {
  static std::mutex uinputMutex;
  std::lock_guard<std::mutex> lock(uinputMutex);
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
  
    Key keysym = StringToVirtualKey(keyName);
    if (keysym == NoSymbol) {
      std::cerr << "Invalid key: " << keyName << std::endl;
      return;
    }
  
    KeyCode keycode = XKeysymToKeycode(display, keysym);
    if (keycode == 0) {
      std::cerr << "Cannot find keycode for " << keyName << std::endl;
      return;
    }
    
    // Add state tracking to X11 as well
    if (press) {
      if (!TryPressKey(keycode)) return; // Already pressed
    } else {
      if (!TryReleaseKey(keycode)) return; // Not pressed
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
      if (!TryPressKey(keycode)) return; // Already pressed
    } else {
      if (!TryReleaseKey(keycode)) return; // Not pressed
    }
    
    if (uinputFd < 0) {
      if (!SetupUinputDevice()) {
        std::cerr << "Failed to initialize uinput device" << std::endl;
        return;
      }
    }
  
    struct input_event ev{};
    gettimeofday(&ev.time, nullptr);
  
    ev.type = EV_KEY;
    ev.code = keycode;
    ev.value = down ? 1 : 0;
    
    if(Configs::Get().GetVerboseKeyLogging())
      debug("Sending uinput key: " + std::to_string(keycode) + " (" +
            std::to_string(down) + ")");
  
    if (write(uinputFd, &ev, sizeof(ev)) < 0) {
      std::cerr << "Failed to write key event\n";
      return;
    }
  
    ev.type = EV_SYN;
    ev.code = SYN_REPORT;
    ev.value = 0;
  
    if (write(uinputFd, &ev, sizeof(ev)) < 0) {
      std::cerr << "Failed to write sync event\n";
    }
  }
  
  // State tracking implementation
  bool IO::TryPressKey(int keycode) {
    std::lock_guard<std::mutex> lock(keyStateMutex);
    if (pressedKeys.count(keycode)) {
      if(Configs::Get().GetVerboseKeyLogging())
        debug("Key " + std::to_string(keycode) + " already pressed, ignoring");
      return false; // Already pressed
    }
    pressedKeys.insert(keycode);
    return true; // OK to press
  }
  
  bool IO::TryReleaseKey(int keycode) {
    std::lock_guard<std::mutex> lock(keyStateMutex);
    if (!pressedKeys.count(keycode)) {
      if(Configs::Get().GetVerboseKeyLogging())
        debug("Key " + std::to_string(keycode) + " not pressed, ignoring release");
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
        {"rshift", "RShift"}, {"alt", "LAlt"}, {"ralt", "RAlt"},
        {"meta", "LMeta"}, {"rmeta", "RMeta"},
    };
  
    std::unordered_map<char, std::string> shorthandModifiers = {
        {'^', "ctrl"}, {'!', "alt"}, {'+', "shift"},
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
          if(Configs::Get().GetVerboseKeyLogging())
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
        } else if (seq.ends_with(" down")) {
          std::string mod = seq.substr(0, seq.size() - 5);
          if (modifierKeys.count(mod)) {
            SendKey(modifierKeys[mod], true);
            activeModifiers.push_back(mod);
          } else {
            SendKey(mod, true);
          }
        } else if (seq.ends_with(" up")) {
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
bool IO::Suspend(){
  try {
    if(isSuspended) {
      for(auto& [id, hotkey] : hotkeys) {
        if(!hotkey.suspend){
          if(!hotkey.evdev){
            Grab(hotkey.key, hotkey.modifiers, DisplayManager::GetRootWindow(), hotkey.exclusive);
          }
          hotkey.enabled = true;
        }
      }
      isSuspended = false;
      return true;
    }
    for(auto& [id, hotkey] : hotkeys) {
      if(!hotkey.suspend){
        if(!hotkey.evdev){
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
HotKey IO::AddHotkey(const std::string &rawInput, std::function<void()> action,
                     int id) const {
  std::string hotkeyStr = rawInput;
  auto wrapped_action = [action, hotkeyStr]() {
    if(Configs::Get().GetVerboseKeyLogging())
      info("Hotkey pressed: " + hotkeyStr);
    action();
  };
  // Parse event type from ":up"/":down"
  HotkeyEventType eventType = HotkeyEventType::Down;
  size_t colonPos = hotkeyStr.rfind(':');
  if (colonPos != std::string::npos && colonPos + 1 < hotkeyStr.size()) {
    std::string suffix = hotkeyStr.substr(colonPos + 1);
    if (suffix == "up")
      eventType = HotkeyEventType::Up;
    hotkeyStr = hotkeyStr.substr(0, colonPos);
  }

  // Generate ID if not provided
  if (id == 0)
    id = ++hotkeyCount;
  // Check for evdev prefix first
  bool isEvdev = false;
  if (!hotkeyStr.empty() && hotkeyStr[0] == '@') {
      isEvdev = true;
      hotkeyStr = hotkeyStr.substr(1); // Remove the '@' prefix
  }

  // Now parse modifiers
  bool exclusive = true;
  bool suspendKey = false;
  int modifiers = 0;
  size_t i = 0;

  while (i < hotkeyStr.size()) {
      switch (hotkeyStr[i]) {
      case '!':
          modifiers |= isEvdev ? KEY_LEFTALT : Mod1Mask;
          break; // Alt
      case '^':
          modifiers |= isEvdev ? KEY_LEFTCTRL : ControlMask;
          break; // Ctrl
      case '+':
          modifiers |= isEvdev ? KEY_LEFTSHIFT : ShiftMask;
          break; // Shift
      case '#':
          modifiers |= isEvdev ? KEY_LEFTMETA : Mod4Mask;
          break; // Win/Super
      case '*':
      case '~':
          exclusive = false;
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
  hotkeyStr = hotkeyStr.substr(i); // Remove modifiers

  // Determine keycode
  KeyCode keycode = 0;
  bool isEvdev = false;

  if (!hotkeyStr.empty() && (hotkeyStr[0] == '@' || globalEvdev)) {
    // evdev mode
    std::string evdevKey;
    if(hotkeyStr[0] == '@')
      evdevKey = hotkeyStr.substr(1);
    else
      evdevKey = hotkeyStr;
    keycode = EvdevNameToKeyCode(evdevKey);
    if (keycode == 0) {
      std::cerr << "Invalid evdev key name: " << evdevKey << "\n";
      return {};
    }
    isEvdev = true;
  } else if (hotkeyStr.substr(0, 2) == "kc") {
    // raw keycode like kc123
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
    // Convert string to keysym first
    KeySym keysym = StringToVirtualKey(keyLower);
    if (keysym == 0) {
      std::cerr << "Invalid key name: " << keyLower << "\n";
      return {};
    }

    // Convert keysym to keycode
    keycode = XKeysymToKeycode(display, keysym);
    if (keycode == 0) {
      std::cerr << "Key '" << keyLower << "' not available on this keyboard layout\n";
      return {};
    }
  }

  // Build hotkey
  HotKey hk;
  hk.alias = rawInput;
  hk.key = static_cast<Key>(keycode);
  hk.modifiers = modifiers;
  hk.callback = std::move(wrapped_action);
  hk.action = "";
  hk.enabled = true;
  hk.blockInput = exclusive;
  hk.suspend = suspendKey;
  hk.exclusive = exclusive;
  hk.success = (display && keycode > 0);
  hk.evdev = isEvdev;
  hk.eventType = eventType;

  hotkeys[id] = hk;
  return hotkeys.at(id);
}
bool IO::Hotkey(const std::string &rawInput, std::function<void()> action,
                int id) {
  HotKey hk = AddHotkey(rawInput, std::move(action), id);

  if (!hk.success) {
    std::cerr << "Failed to register hotkey: " << rawInput << "\n";
    return false;
  }

  std::cout << "Registered hotkey: " << rawInput
            << (hk.suspend ? " (suspend key)" : "") << "\n  Key: " << hk.key
            << ", Modifiers: " << hk.modifiers
            << ", Exclusive: " << hk.exclusive << ", EventType: "
            << (hk.eventType == HotkeyEventType::Down ? "Down"
                : hk.eventType == HotkeyEventType::Up ? "Up"
                                                      : "Both")
            << "\n";

  if (!hk.evdev && display) {
    Grab(hk.key, hk.modifiers, DefaultRootWindow(display), hk.exclusive);
  }

  return true;
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
  if (keyName == "numpaddec")
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
  if (XGrabKey(display, keycode, hotkey.modifiers, root, x11::XFalse, GrabModeAsync,
               GrabModeAsync) != x11::XSuccess) {
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
      if (!hotkey.blockInput && hotkey.enabled) {
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

void IO::PressKey(const std::string &keyName, bool press) {
  std::cout << "Pressing key: " << keyName << " (press: " << press << ")"
            << std::endl;

#ifdef __linux__
  Display *display = havel::DisplayManager::GetDisplay();
  if (!display) {
    std::cerr << "No X11 display available for key press\n";
    return;
  }

  // Convert string to keysym
  KeySym keysym = StringToVirtualKey(keyName);  
  if (keysym == NoSymbol) {
    std::cerr << "Unknown keysym for: " << keyName << "\n";
    return;
  }

  // Convert keysym to keycode
  KeyCode keycode = XKeysymToKeycode(display, keysym);
  if (keycode == 0) {
    std::cerr << "Invalid keycode for keysym: " << keyName << "\n";
    return;
  }

  // Send fake key event
  XTestFakeKeyEvent(display, keycode, press ? x11::XTrue : x11::XFalse, CurrentTime);
  XFlush(display);
#endif
}

bool IO::AddHotkey(const std::string &alias, Key key, int modifiers,
                   std::function<void()> callback) {
  // Stub implementation for AddHotkey
  std::cout << "Adding hotkey: " << alias << std::endl;
  HotKey hotkey;
  hotkey.alias = alias;
  hotkey.key = key;
  hotkey.modifiers = modifiers;
  hotkey.callback = callback;

  hotkeyCount++;
  hotkeys[hotkeyCount] = hotkey;
  return true;
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
    Grab(keycode, hotkey.modifiers, root, hotkey.exclusive);
  }

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
    std::cerr << "Hotkey ID not found: " << hotkeyId << std::endl;
    return false;
  }
  const HotKey &hotkey = it->second;
  info("Ungrabbing hotkey: " + hotkey.alias);
  Window root = DefaultRootWindow(display);
  KeyCode keycode = hotkey.key;

  if (keycode == 0) {
    std::cerr << "Invalid keycode for hotkey: " << hotkey.alias << std::endl;
    return false;
  }

  // Use our improved method to ungrab with all modifier variants
  if (!hotkey.evdev) {
    Ungrab(keycode, hotkey.modifiers, root);
  }

  std::cout << "Successfully ungrabbed hotkey: " << hotkey.alias << std::endl;
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

void IO::Map(const std::string& from, const std::string& to) {
    KeySym fromKey = StringToVirtualKey(from);
    KeySym toKey = StringToVirtualKey(to);
    if (fromKey != NoSymbol && toKey != NoSymbol) {
        keyMapInternal[fromKey] = toKey;
    }
}

void IO::Remap(const std::string& key1, const std::string& key2) {
    KeySym k1 = StringToVirtualKey(key1);
    KeySym k2 = StringToVirtualKey(key2);
    if (k1 != NoSymbol && k2 != NoSymbol) {
        remappedKeys[k1] = k2;
        remappedKeys[k2] = k1;
    }
}
bool IO::MatchEvdevModifiers(int expectedModifiers, const std::map<int, bool>& keyState) {
  // Check each modifier bit
  if ((expectedModifiers & KEY_LEFTCTRL) && 
      !(keyState.at(KEY_LEFTCTRL) || keyState.at(KEY_RIGHTCTRL))) {
      return false;
  }
  
  if ((expectedModifiers & KEY_LEFTSHIFT) && 
      !(keyState.at(KEY_LEFTSHIFT) || keyState.at(KEY_RIGHTSHIFT))) {
      return false;
  }
  
  if ((expectedModifiers & KEY_LEFTALT) && 
      !(keyState.at(KEY_LEFTALT) || keyState.at(KEY_RIGHTALT))) {
      return false;
  }
  
  if ((expectedModifiers & KEY_LEFTMETA) && 
      !(keyState.at(KEY_LEFTMETA) || keyState.at(KEY_RIGHTMETA))) {
      return false;
  }
  
  // Check that no unexpected modifiers are pressed
  if ((expectedModifiers & KEY_LEFTCTRL) == 0 && 
      (keyState.at(KEY_LEFTCTRL) || keyState.at(KEY_RIGHTCTRL))) {
      return false;
  }
  
  if ((expectedModifiers & KEY_LEFTSHIFT) == 0 && 
      (keyState.at(KEY_LEFTSHIFT) || keyState.at(KEY_RIGHTSHIFT))) {
      return false;
  }
  
  if ((expectedModifiers & KEY_LEFTALT) == 0 && 
      (keyState.at(KEY_LEFTALT) || keyState.at(KEY_RIGHTALT))) {
      return false;
  }
  
  if ((expectedModifiers & KEY_LEFTMETA) == 0 && 
      (keyState.at(KEY_LEFTMETA) || keyState.at(KEY_RIGHTMETA))) {
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
      error("evdev: cannot open " + evdevDevicePath + ": " + std::string(strerror(errno)) + "\n");
      evdevRunning = false;
      return;
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

      // Now aggregate modifiers
      modState[ControlMask] =
          evdevKeyState[KEY_LEFTCTRL] || evdevKeyState[KEY_RIGHTCTRL];
      modState[ShiftMask] =
          evdevKeyState[KEY_LEFTSHIFT] || evdevKeyState[KEY_RIGHTSHIFT];
      modState[Mod1Mask] =
          evdevKeyState[KEY_LEFTALT] || evdevKeyState[KEY_RIGHTALT];
      modState[Mod4Mask] =
          evdevKeyState[KEY_LEFTMETA] || evdevKeyState[KEY_RIGHTMETA];

      keyDownState[code] = down;

      // Dispatch both key down and key up events based on registration
      std::vector<std::function<void()>> callbacks;
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
          // Exact modifier matching
          if (!MatchEvdevModifiers(hotkey.modifiers, evdevKeyState))
            continue;
          // Context checks
          if (!hotkey.contexts.empty()) {
            if (!std::all_of(hotkey.contexts.begin(), hotkey.contexts.end(),
                             [](auto &ctx) { return ctx(); })) {
              continue;
            }
          }
          hotkey.success = true;
          if (hotkey.callback) {
            callbacks.push_back(hotkey.callback);
          }
        }
      }
      // Execute all callbacks without holding the lock
      for (auto &callback : callbacks) {
        try {
          callback();
        } catch (const std::exception &e) {
          error("Error in hotkey callback: " + std::string(e.what()));
        }
      }
      SendUInput(code, down);
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
  if (uinputFd >= 0) {
    ioctl(uinputFd, UI_DEV_DESTROY);
    close(uinputFd);
    uinputFd = -1;
  }
}
} // namespace havel
