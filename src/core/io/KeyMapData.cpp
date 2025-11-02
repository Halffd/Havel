#include "KeyMap.hpp"
#include <X11/XF86keysym.h>

// This file contains all the key mapping data extracted from IO.cpp
// It populates the KeyMap with comprehensive mappings for evdev, X11, and Windows

namespace havel {

// Helper to add all key mappings
void KeyMap::Initialize() {
    if (initialized) return;
    
    // Windows VK codes (for reference when WINDOWS is not defined)
    #ifndef WINDOWS
    #define VK_ESCAPE 0x1B
    #define VK_RETURN 0x0D
    #define VK_SPACE 0x20
    #define VK_TAB 0x09
    #define VK_BACK 0x08
    #define VK_DELETE 0x2E
    #define VK_CONTROL 0x11
    #define VK_LCONTROL 0xA2
    #define VK_RCONTROL 0xA3
    #define VK_SHIFT 0x10
    #define VK_LSHIFT 0xA0
    #define VK_RSHIFT 0xA1
    #define VK_MENU 0x12
    #define VK_LMENU 0xA4
    #define VK_RMENU 0xA5
    #define VK_LWIN 0x5B
    #define VK_RWIN 0x5C
    #define VK_HOME 0x24
    #define VK_END 0x23
    #define VK_PRIOR 0x21
    #define VK_NEXT 0x22
    #define VK_INSERT 0x2D
    #define VK_LEFT 0x25
    #define VK_RIGHT 0x27
    #define VK_UP 0x26
    #define VK_DOWN 0x28
    #define VK_CAPITAL 0x14
    #define VK_NUMLOCK 0x90
    #define VK_SCROLL 0x91
    #define VK_F1 0x70
    #define VK_F2 0x71
    #define VK_F3 0x72
    #define VK_F4 0x73
    #define VK_F5 0x74
    #define VK_F6 0x75
    #define VK_F7 0x76
    #define VK_F8 0x77
    #define VK_F9 0x78
    #define VK_F10 0x79
    #define VK_F11 0x7A
    #define VK_F12 0x7B
    #define VK_F13 0x7C
    #define VK_F14 0x7D
    #define VK_F15 0x7E
    #define VK_F16 0x7F
    #define VK_F17 0x80
    #define VK_F18 0x81
    #define VK_F19 0x82
    #define VK_F20 0x83
    #define VK_F21 0x84
    #define VK_F22 0x85
    #define VK_F23 0x86
    #define VK_F24 0x87
    #define VK_NUMPAD0 0x60
    #define VK_NUMPAD1 0x61
    #define VK_NUMPAD2 0x62
    #define VK_NUMPAD3 0x63
    #define VK_NUMPAD4 0x64
    #define VK_NUMPAD5 0x65
    #define VK_NUMPAD6 0x66
    #define VK_NUMPAD7 0x67
    #define VK_NUMPAD8 0x68
    #define VK_NUMPAD9 0x69
    #define VK_ADD 0x6B
    #define VK_SUBTRACT 0x6D
    #define VK_MULTIPLY 0x6A
    #define VK_DIVIDE 0x6F
    #define VK_DECIMAL 0x6E
    #define VK_SEPARATOR 0x6C
    #define VK_OEM_MINUS 0xBD
    #define VK_OEM_PLUS 0xBB
    #define VK_OEM_4 0xDB
    #define VK_OEM_6 0xDD
    #define VK_OEM_1 0xBA
    #define VK_OEM_7 0xDE
    #define VK_OEM_3 0xC0
    #define VK_OEM_5 0xDC
    #define VK_OEM_COMMA 0xBC
    #define VK_OEM_PERIOD 0xBE
    #define VK_OEM_2 0xBF
    #define VK_OEM_102 0xE2
    #define VK_MEDIA_PLAY_PAUSE 0xB3
    #define VK_PLAY 0xFA
    #define VK_PAUSE 0x13
    #define VK_MEDIA_STOP 0xB2
    #define VK_MEDIA_NEXT_TRACK 0xB0
    #define VK_MEDIA_PREV_TRACK 0xB1
    #define VK_VOLUME_UP 0xAF
    #define VK_VOLUME_DOWN 0xAE
    #define VK_VOLUME_MUTE 0xAD
    #define VK_BROWSER_HOME 0xAC
    #define VK_BROWSER_BACK 0xA6
    #define VK_BROWSER_FORWARD 0xA7
    #define VK_BROWSER_SEARCH 0xAA
    #define VK_BROWSER_FAVORITES 0xAB
    #define VK_BROWSER_REFRESH 0xA8
    #define VK_BROWSER_STOP 0xA9
    #define VK_LAUNCH_MAIL 0xB4
    #define VK_LAUNCH_APP2 0xB7
    #define VK_LAUNCH_MEDIA_SELECT 0xB5
    #define VK_SLEEP 0x5F
    #define VK_PRINT 0x2A
    #define VK_HELP 0x2F
    #define VK_APPS 0x5D
    #define VK_SELECT 0x29
    #define VK_CANCEL 0x03
    #define VK_SNAPSHOT 0x2C
    #endif
    
    // Alphabet keys (A-Z)
    for (char c = 'a'; c <= 'z'; c++) {
        std::string key(1, c);
        AddKey(key, KEY_A + (c - 'a'), XK_a + (c - 'a'), 0x41 + (c - 'a'));
    }
    
    // Number keys (0-9)
    AddKey("0", KEY_0, XK_0, 0x30);
    for (int i = 1; i <= 9; i++) {
        AddKey(std::to_string(i), KEY_1 + (i - 1), XK_1 + (i - 1), 0x31 + (i - 1));
    }
    
    // Control keys
    AddKey("esc", KEY_ESC, XK_Escape, VK_ESCAPE);
    AddAlias("escape", "esc");
    AddKey("enter", KEY_ENTER, XK_Return, VK_RETURN);
    AddAlias("return", "enter");
    AddKey("space", KEY_SPACE, XK_space, VK_SPACE);
    AddKey("tab", KEY_TAB, XK_Tab, VK_TAB);
    AddKey("backspace", KEY_BACKSPACE, XK_BackSpace, VK_BACK);
    AddKey("delete", KEY_DELETE, XK_Delete, VK_DELETE);
    
    // Modifiers
    AddKey("ctrl", KEY_LEFTCTRL, XK_Control_L, VK_CONTROL);
    AddKey("lctrl", KEY_LEFTCTRL, XK_Control_L, VK_LCONTROL);
    AddKey("rctrl", KEY_RIGHTCTRL, XK_Control_R, VK_RCONTROL);
    AddKey("shift", KEY_LEFTSHIFT, XK_Shift_L, VK_SHIFT);
    AddKey("lshift", KEY_LEFTSHIFT, XK_Shift_L, VK_LSHIFT);
    AddKey("rshift", KEY_RIGHTSHIFT, XK_Shift_R, VK_RSHIFT);
    AddKey("alt", KEY_LEFTALT, XK_Alt_L, VK_MENU);
    AddKey("lalt", KEY_LEFTALT, XK_Alt_L, VK_LMENU);
    AddKey("ralt", KEY_RIGHTALT, XK_Alt_R, VK_RMENU);
    AddKey("win", KEY_LEFTMETA, XK_Super_L, VK_LWIN);
    AddAlias("meta", "win");
    AddAlias("lwin", "win");
    AddAlias("lmeta", "win");
    AddAlias("super", "win");
    AddKey("rwin", KEY_RIGHTMETA, XK_Super_R, VK_RWIN);
    AddAlias("rmeta", "rwin");
    
    // Navigation
    AddKey("home", KEY_HOME, XK_Home, VK_HOME);
    AddKey("end", KEY_END, XK_End, VK_END);
    AddKey("pgup", KEY_PAGEUP, XK_Page_Up, VK_PRIOR);
    AddAlias("pageup", "pgup");
    AddKey("pgdn", KEY_PAGEDOWN, XK_Page_Down, VK_NEXT);
    AddAlias("pagedown", "pgdn");
    AddKey("insert", KEY_INSERT, XK_Insert, VK_INSERT);
    AddKey("left", KEY_LEFT, XK_Left, VK_LEFT);
    AddKey("right", KEY_RIGHT, XK_Right, VK_RIGHT);
    AddKey("up", KEY_UP, XK_Up, VK_UP);
    AddKey("down", KEY_DOWN, XK_Down, VK_DOWN);
    
    // Lock keys
    AddKey("capslock", KEY_CAPSLOCK, XK_Caps_Lock, VK_CAPITAL);
    AddKey("numlock", KEY_NUMLOCK, XK_Num_Lock, VK_NUMLOCK);
    AddKey("scrolllock", KEY_SCROLLLOCK, XK_Scroll_Lock, VK_SCROLL);
    
    // Function keys (F1-F24)
    for (int i = 1; i <= 24; i++) {
        std::string fname = "f" + std::to_string(i);
        AddKey(fname, KEY_F1 + (i - 1), XK_F1 + (i - 1), VK_F1 + (i - 1));
    }
    
    // Numpad
    AddKey("numpad0", KEY_KP0, XK_KP_0, VK_NUMPAD0);
    for (int i = 1; i <= 9; i++) {
        AddKey("numpad" + std::to_string(i), KEY_KP1 + (i - 1), XK_KP_1 + (i - 1), VK_NUMPAD1 + (i - 1));
    }
    AddKey("numpadadd", KEY_KPPLUS, XK_KP_Add, VK_ADD);
    AddAlias("numpadplus", "numpadadd");
    AddKey("numpadsub", KEY_KPMINUS, XK_KP_Subtract, VK_SUBTRACT);
    AddAlias("numpadminus", "numpadsub");
    AddKey("numpadmul", KEY_KPASTERISK, XK_KP_Multiply, VK_MULTIPLY);
    AddAlias("numpadmult", "numpadmul");
    AddAlias("numpadasterisk", "numpadmul");
    AddKey("numpaddiv", KEY_KPSLASH, XK_KP_Divide, VK_DIVIDE);
    AddKey("numpaddec", KEY_KPDOT, XK_KP_Decimal, VK_DECIMAL);
    AddAlias("numpaddot", "numpaddec");
    AddAlias("numpaddel", "numpaddec");
    AddAlias("numpadperiod", "numpaddec");
    AddAlias("numpaddelete", "numpaddec");
    AddAlias("numpaddecimal", "numpaddec");
    AddKey("numpadenter", KEY_KPENTER, XK_KP_Enter, VK_RETURN);
    AddKey("numpadequal", KEY_KPEQUAL, XK_KP_Equal, 0);
    AddKey("numpadcomma", KEY_KPCOMMA, XK_KP_Separator, VK_SEPARATOR);
    AddKey("numpadleftparen", KEY_KPLEFTPAREN, 0, 0);
    AddKey("numpadrightparen", KEY_KPRIGHTPAREN, 0, 0);
    
    // Symbols
    AddKey("minus", KEY_MINUS, XK_minus, VK_OEM_MINUS);
    AddAlias("-", "minus");
    AddKey("equal", KEY_EQUAL, XK_equal, VK_OEM_PLUS);
    AddAlias("equals", "equal");
    AddAlias("=", "equal");
    AddKey("leftbrace", KEY_LEFTBRACE, XK_bracketleft, VK_OEM_4);
    AddAlias("[", "leftbrace");
    AddKey("rightbrace", KEY_RIGHTBRACE, XK_bracketright, VK_OEM_6);
    AddAlias("]", "rightbrace");
    AddKey("semicolon", KEY_SEMICOLON, XK_semicolon, VK_OEM_1);
    AddAlias(";", "semicolon");
    AddKey("apostrophe", KEY_APOSTROPHE, XK_apostrophe, VK_OEM_7);
    AddAlias("'", "apostrophe");
    AddKey("grave", KEY_GRAVE, XK_grave, VK_OEM_3);
    AddAlias("`", "grave");
    AddKey("backslash", KEY_BACKSLASH, XK_backslash, VK_OEM_5);
    AddAlias("\\", "backslash");
    AddKey("comma", KEY_COMMA, XK_comma, VK_OEM_COMMA);
    AddAlias(",", "comma");
    AddKey("dot", KEY_DOT, XK_period, VK_OEM_PERIOD);
    AddAlias("period", "dot");
    AddAlias(".", "dot");
    AddKey("slash", KEY_SLASH, XK_slash, VK_OEM_2);
    AddAlias("/", "slash");
    AddKey("less", KEY_102ND, 0, VK_OEM_102);
    AddAlias("<", "less");
    AddAlias("102nd", "less");
    AddAlias("iso", "less");
    
    // Media control keys
    AddKey("playpause", KEY_PLAYPAUSE, XF86XK_AudioPlay, VK_MEDIA_PLAY_PAUSE);
    AddAlias("mediaplay", "playpause");
    AddKey("play", KEY_PLAY, XF86XK_AudioPlay, VK_PLAY);
    AddKey("pause", KEY_PAUSE, XK_Pause, VK_PAUSE);
    AddKey("stop", KEY_STOP, 0, 0);
    AddKey("stopcd", KEY_STOPCD, XF86XK_AudioStop, VK_MEDIA_STOP);
    AddAlias("mediastop", "stopcd");
    AddKey("record", KEY_RECORD, XF86XK_AudioRecord, 0);
    AddAlias("mediarecord", "record");
    AddKey("rewind", KEY_REWIND, 0, 0);
    AddAlias("mediarewind", "rewind");
    AddKey("fastforward", KEY_FASTFORWARD, 0, 0);
    AddAlias("mediaforward", "fastforward");
    AddKey("ejectcd", KEY_EJECTCD, XF86XK_Eject, 0);
    AddAlias("eject", "ejectcd");
    AddAlias("mediaeject", "ejectcd");
    AddKey("nextsong", KEY_NEXTSONG, XF86XK_AudioNext, VK_MEDIA_NEXT_TRACK);
    AddAlias("next", "nextsong");
    AddAlias("medianext", "nextsong");
    AddKey("previoussong", KEY_PREVIOUSSONG, XF86XK_AudioPrev, VK_MEDIA_PREV_TRACK);
    AddAlias("prev", "previoussong");
    AddAlias("previous", "previoussong");
    AddAlias("mediaprev", "previoussong");
    
    // Volume control
    AddKey("volumeup", KEY_VOLUMEUP, XF86XK_AudioRaiseVolume, VK_VOLUME_UP);
    AddKey("volumedown", KEY_VOLUMEDOWN, XF86XK_AudioLowerVolume, VK_VOLUME_DOWN);
    AddKey("mute", KEY_MUTE, XF86XK_AudioMute, VK_VOLUME_MUTE);
    AddAlias("volumemute", "mute");
    AddKey("micmute", KEY_MICMUTE, XF86XK_AudioMicMute, 0);
    
    // Browser keys
    AddKey("homepage", KEY_HOMEPAGE, XF86XK_HomePage, VK_BROWSER_HOME);
    AddKey("back", KEY_BACK, XF86XK_Back, VK_BROWSER_BACK);
    AddKey("forward", KEY_FORWARD, XF86XK_Forward, VK_BROWSER_FORWARD);
    AddKey("search", KEY_SEARCH, XF86XK_Search, VK_BROWSER_SEARCH);
    AddKey("bookmarks", KEY_BOOKMARKS, XF86XK_Favorites, VK_BROWSER_FAVORITES);
    AddKey("refresh", KEY_REFRESH, XF86XK_Refresh, VK_BROWSER_REFRESH);
    AddKey("stopbrowser", KEY_STOP, XF86XK_Stop, VK_BROWSER_STOP);
    AddKey("favorites", KEY_FAVORITES, XF86XK_Favorites, VK_BROWSER_FAVORITES);
    
    // Application launcher keys
    AddKey("mail", KEY_MAIL, XF86XK_Mail, VK_LAUNCH_MAIL);
    AddKey("calc", KEY_CALC, XF86XK_Calculator, VK_LAUNCH_APP2);
    AddAlias("calculator", "calc");
    AddKey("computer", KEY_COMPUTER, XF86XK_MyComputer, 0);
    AddKey("media", KEY_MEDIA, 0, VK_LAUNCH_MEDIA_SELECT);
    AddKey("www", KEY_WWW, XF86XK_WWW, 0);
    AddKey("finance", KEY_FINANCE, XF86XK_Finance, 0);
    AddKey("shop", KEY_SHOP, XF86XK_Shop, 0);
    AddKey("coffee", KEY_COFFEE, 0, 0);
    AddKey("chat", KEY_CHAT, 0, 0);
    AddKey("messenger", KEY_MESSENGER, XF86XK_Messenger, 0);
    AddKey("calendar", KEY_CALENDAR, 0, 0);
    
    // Power management
    AddKey("power", KEY_POWER, XF86XK_PowerOff, 0);
    AddKey("sleep", KEY_SLEEP, XF86XK_Sleep, VK_SLEEP);
    AddKey("wakeup", KEY_WAKEUP, XF86XK_WakeUp, 0);
    AddKey("suspend", KEY_SUSPEND, 0, 0);
    
    // Display/brightness
    AddKey("brightnessup", KEY_BRIGHTNESSUP, XF86XK_MonBrightnessUp, 0);
    AddKey("brightnessdown", KEY_BRIGHTNESSDOWN, XF86XK_MonBrightnessDown, 0);
    AddKey("brightness", KEY_BRIGHTNESS_AUTO, 0, 0);
    AddAlias("brightnessauto", "brightness");
    AddKey("displayoff", KEY_DISPLAY_OFF, 0, 0);
    AddKey("switchvideomode", KEY_SWITCHVIDEOMODE, 0, 0);
    
    // Keyboard backlight
    AddKey("kbdillumup", KEY_KBDILLUMUP, XF86XK_KbdBrightnessUp, 0);
    AddKey("kbdillumdown", KEY_KBDILLUMDOWN, XF86XK_KbdBrightnessDown, 0);
    AddKey("kbdillumtoggle", KEY_KBDILLUMTOGGLE, 0, 0);
    
    // Wireless
    AddKey("wlan", KEY_WLAN, XF86XK_WLAN, 0);
    AddAlias("wifi", "wlan");
    AddKey("bluetooth", KEY_BLUETOOTH, XF86XK_Bluetooth, 0);
    AddKey("rfkill", KEY_RFKILL, 0, 0);
    
    // Battery
    AddKey("battery", KEY_BATTERY, XF86XK_Battery, 0);
    
    // Zoom
    AddKey("zoomin", KEY_ZOOMIN, XF86XK_ZoomIn, 0);
    AddKey("zoomout", KEY_ZOOMOUT, XF86XK_ZoomOut, 0);
    AddKey("zoomreset", KEY_ZOOMRESET, 0, 0);
    
    // Screen control
    AddKey("cyclewindows", KEY_CYCLEWINDOWS, 0, 0);
    AddKey("scale", KEY_SCALE, 0, 0);
    AddKey("dashboard", KEY_DASHBOARD, 0, 0);
    
    // File operations
    AddKey("file", KEY_FILE, XF86XK_Documents, 0);
    AddKey("open", KEY_OPEN, XF86XK_Open, 0);
    AddKey("close", KEY_CLOSE, XF86XK_Close, 0);
    AddKey("save", KEY_SAVE, XF86XK_Save, 0);
    AddKey("print", KEY_PRINT, XK_Print, VK_PRINT);
    AddKey("cut", KEY_CUT, 0, 0);
    AddKey("copy", KEY_COPY, XF86XK_Copy, 0);
    AddKey("paste", KEY_PASTE, XF86XK_Paste, 0);
    AddKey("find", KEY_FIND, 0, 0);
    AddKey("undo", KEY_UNDO, 0, 0);
    AddKey("redo", KEY_REDO, 0, 0);
    
    // Text editing
    AddKey("again", KEY_AGAIN, 0, 0);
    AddKey("props", KEY_PROPS, 0, 0);
    AddKey("front", KEY_FRONT, 0, 0);
    AddKey("help", KEY_HELP, XK_Help, VK_HELP);
    AddKey("menu", KEY_MENU, XK_Menu, VK_APPS);
    AddAlias("apps", "menu");
    AddKey("select", KEY_SELECT, 0, VK_SELECT);
    AddKey("cancel", KEY_CANCEL, 0, VK_CANCEL);
    
    // ISO keyboard extras
    AddKey("ro", KEY_RO, 0, 0);
    AddKey("katakanahiragana", KEY_KATAKANAHIRAGANA, 0, 0);
    AddKey("yen", KEY_YEN, 0, 0);
    AddKey("henkan", KEY_HENKAN, 0, 0);
    AddKey("muhenkan", KEY_MUHENKAN, 0, 0);
    AddKey("kpjpcomma", KEY_KPJPCOMMA, 0, 0);
    AddKey("hangeul", KEY_HANGEUL, 0, 0);
    AddKey("hanja", KEY_HANJA, 0, 0);
    AddKey("katakana", KEY_KATAKANA, 0, 0);
    AddKey("hiragana", KEY_HIRAGANA, 0, 0);
    AddKey("zenkakuhankaku", KEY_ZENKAKUHANKAKU, 0, 0);
    
    // Special system keys
    AddKey("sysrq", KEY_SYSRQ, XK_Sys_Req, 0);
    AddKey("printscreen", KEY_SYSRQ, XK_Print, VK_SNAPSHOT);
    AddKey("pausebreak", KEY_PAUSE, XK_Pause, VK_PAUSE);
    AddKey("scrollup", KEY_SCROLLUP, 0, 0);
    AddKey("scrolldown", KEY_SCROLLDOWN, 0, 0);
    
    // Gaming/multimedia extras
    AddKey("prog1", KEY_PROG1, 0, 0);
    AddKey("prog2", KEY_PROG2, 0, 0);
    AddKey("prog3", KEY_PROG3, 0, 0);
    AddKey("prog4", KEY_PROG4, 0, 0);
    AddKey("macro", KEY_MACRO, 0, 0);
    AddKey("fn", KEY_FN, 0, 0);
    AddKey("fnesc", KEY_FN_ESC, 0, 0);
    for (int i = 1; i <= 12; i++) {
        AddKey("fnf" + std::to_string(i), KEY_FN_F1 + (i - 1), 0, 0);
    }
    
    // Mouse buttons
    AddKey("lbutton", BTN_LEFT, Button1, 0);
    AddKey("rbutton", BTN_RIGHT, Button3, 0);
    AddKey("mbutton", BTN_MIDDLE, Button2, 0);
    AddKey("xbutton1", BTN_SIDE, Button4, 0);
    AddAlias("side1", "xbutton1");
    AddKey("xbutton2", BTN_EXTRA, Button5, 0);
    AddAlias("side2", "xbutton2");
    
    // Mouse wheel (special handling, no direct evdev code)
    AddKey("wheelup", 0, Button4, 0);
    AddAlias("scrollup", "wheelup");
    AddKey("wheeldown", 0, Button5, 0);
    AddAlias("scrolldown", "wheeldown");
    
    // Joystick buttons
    AddKey("joya", BTN_SOUTH, 0, 0);
    AddKey("joyb", BTN_EAST, 0, 0);
    AddKey("joyx", BTN_WEST, 0, 0);
    AddKey("joyy", BTN_NORTH, 0, 0);
    AddKey("joylb", BTN_TL, 0, 0);
    AddKey("joyrb", BTN_TR, 0, 0);
    AddKey("joylt", BTN_TL2, 0, 0);
    AddKey("joyrt", BTN_TR2, 0, 0);
    AddKey("joyback", BTN_SELECT, 0, 0);
    AddKey("joystart", BTN_START, 0, 0);
    AddKey("joyguide", BTN_MODE, 0, 0);
    AddKey("joylstick", BTN_THUMBL, 0, 0);
    AddKey("joyrstick", BTN_THUMBR, 0, 0);
    AddKey("joydpadup", BTN_DPAD_UP, 0, 0);
    AddKey("joydpaddown", BTN_DPAD_DOWN, 0, 0);
    AddKey("joydpadleft", BTN_DPAD_LEFT, 0, 0);
    AddKey("joydpadright", BTN_DPAD_RIGHT, 0, 0);
    
    // Special marker
    AddKey("reserved", KEY_RESERVED, 0, 0);
    AddKey("unknown", KEY_UNKNOWN, 0, 0);
    AddAlias("nosymbol", "unknown");
    
    initialized = true;
}

} // namespace havel
