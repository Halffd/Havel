#pragma once

/*
 * input_event_codes.hpp - evdev input codes on a platform without evdev.
 *
 * <linux/input-event-codes.h> is the authority for every KEY_/BTN_/REL_/ABS_/
 * EV_ constant in this tree. On Linux this header just pulls it in and adds
 * nothing, so the kernel stays the single source of truth.
 *
 * Windows has no such header. The values below are the kernel's own, copied
 * verbatim rather than invented, because they are not mere placeholders: they
 * are compared against the codes a Linux evdev device reports, and Havel's hotkey
 * parsing maps a name like "ctrl" onto one of these. If a value here drifted
 * from the kernel's, the same hotkey would resolve to a different physical key
 * on the two platforms, which is exactly the class of bug this file exists to
 * prevent. Change a value only to match the kernel header.
 *
 * Before this existed the same block was spelled out in Device.hpp while
 * KeyMap.hpp relied on the kernel header directly, so the two disagreed on
 * availability and one of them on values. There is now one copy.
 */

#include "havel_platform.h"

#if HAVEL_PLATFORM_LINUX
#include <linux/input-event-codes.h>
#elif HAVEL_PLATFORM_WINDOWS

/* --- Event types (linux/input.h) ---------------------------------------- */
#define EV_SYN 0x00
#define EV_KEY 0x01
#define EV_REL 0x02
#define EV_ABS 0x03
#define EV_MSC 0x04
#define EV_SW 0x05
#define EV_LED 0x11
#define EV_SND 0x12
#define EV_REP 0x14
#define EV_FF 0x15
#define EV_PWR 0x16
#define EV_FF_STATUS 0x17

/* --- Synchronisation events --------------------------------------------- */
#define SYN_REPORT 0
#define SYN_CONFIG 1
#define SYN_MT_REPORT 2

/* --- Misc events --------------------------------------------------------- */
#define MSC_SERIAL 0x00
#define MSC_PULSELED 0x01
#define MSC_GESTURE 0x02
#define MSC_RAW 0x03
#define MSC_SCAN 0x04
#define MSC_TIMESTAMP 0x05

/* --- Keyboard ----------------------------------------------------------- */
#define KEY_RESERVED 0
#define KEY_ESC 1
#define KEY_1 2
#define KEY_2 3
#define KEY_3 4
#define KEY_4 5
#define KEY_5 6
#define KEY_6 7
#define KEY_7 8
#define KEY_8 9
#define KEY_9 10
#define KEY_0 11
#define KEY_MINUS 12
#define KEY_EQUAL 13
#define KEY_BACKSPACE 14
#define KEY_TAB 15
#define KEY_Q 16
#define KEY_W 17
#define KEY_E 18
#define KEY_R 19
#define KEY_T 20
#define KEY_Y 21
#define KEY_U 22
#define KEY_I 23
#define KEY_O 24
#define KEY_P 25
#define KEY_LEFTBRACE 26
#define KEY_RIGHTBRACE 27
#define KEY_ENTER 28
#define KEY_LEFTCTRL 29
#define KEY_A 30
#define KEY_S 31
#define KEY_D 32
#define KEY_F 33
#define KEY_G 34
#define KEY_H 35
#define KEY_J 36
#define KEY_K 37
#define KEY_L 38
#define KEY_SEMICOLON 39
#define KEY_APOSTROPHE 40
#define KEY_GRAVE 41
#define KEY_LEFTSHIFT 42
#define KEY_BACKSLASH 43
#define KEY_Z 44
#define KEY_X 45
#define KEY_C 46
#define KEY_V 47
#define KEY_B 48
#define KEY_N 49
#define KEY_M 50
#define KEY_COMMA 51
#define KEY_DOT 52
#define KEY_SLASH 53
#define KEY_RIGHTSHIFT 54
#define KEY_KPASTERISK 55
#define KEY_LEFTALT 56
#define KEY_SPACE 57
#define KEY_CAPSLOCK 58
#define KEY_F1 59
#define KEY_F2 60
#define KEY_F3 61
#define KEY_F4 62
#define KEY_F5 63
#define KEY_F6 64
#define KEY_F7 65
#define KEY_F8 66
#define KEY_F9 67
#define KEY_F10 68
#define KEY_NUMLOCK 69
#define KEY_SCROLLLOCK 70
#define KEY_KP7 71
#define KEY_KP8 72
#define KEY_KP9 73
#define KEY_KPMINUS 74
#define KEY_KP4 75
#define KEY_KP5 76
#define KEY_KP6 77
#define KEY_KPPLUS 78
#define KEY_KP1 79
#define KEY_KP2 80
#define KEY_KP3 81
#define KEY_KP0 82
#define KEY_KPDOT 83
#define KEY_102ND 86
#define KEY_F11 87
#define KEY_F12 88
#define KEY_KPENTER 96
#define KEY_RIGHTCTRL 97
#define KEY_KPSLASH 98
#define KEY_SYSRQ 99
#define KEY_RIGHTALT 100
#define KEY_LINEFEED 101
#define KEY_HOME 102
#define KEY_UP 103
#define KEY_PAGEUP 104
#define KEY_LEFT 105
#define KEY_RIGHT 106
#define KEY_END 107
#define KEY_DOWN 108
#define KEY_PAGEDOWN 109
#define KEY_INSERT 110
#define KEY_DELETE 111
#define KEY_MACRO 112
#define KEY_MUTE 113
#define KEY_VOLUMEDOWN 114
#define KEY_VOLUMEUP 115
#define KEY_POWER 116
#define KEY_KPEQUAL 117
#define KEY_KPPLUSMINUS 118
#define KEY_PAUSE 119
#define KEY_KPCOMMA 121
#define KEY_YEN 124
#define KEY_LEFTMETA 125
#define KEY_RIGHTMETA 126
#define KEY_COMPOSE 127
#define KEY_STOP 128
#define KEY_AGAIN 129
#define KEY_PROPS 130
#define KEY_UNDO 131
#define KEY_FRONT 132
#define KEY_COPY 133
#define KEY_OPEN 134
#define KEY_PASTE 135
#define KEY_FIND 136
#define KEY_CUT 137
#define KEY_HELP 138
#define KEY_MENU 139
#define KEY_F13 183
#define KEY_F14 184
#define KEY_F15 185
#define KEY_F16 186
#define KEY_F17 187
#define KEY_F18 188
#define KEY_F19 189
#define KEY_F20 190
#define KEY_F21 191
#define KEY_F22 192
#define KEY_F23 193
#define KEY_F24 194

/* --- Relative axes ------------------------------------------------------- */
#define REL_X 0x00
#define REL_Y 0x01
#define REL_Z 0x02
#define REL_RX 0x03
#define REL_RY 0x04
#define REL_RZ 0x05
#define REL_HWHEEL 0x06
#define REL_DIAL 0x07
#define REL_WHEEL 0x08
#define REL_MISC 0x09

/* --- Absolute axes ------------------------------------------------------- */
#define ABS_X 0x00
#define ABS_Y 0x01
#define ABS_Z 0x02
#define ABS_RX 0x03
#define ABS_RY 0x04
#define ABS_RZ 0x05
#define ABS_THROTTLE 0x06
#define ABS_RUDDER 0x07
#define ABS_WHEEL 0x08
#define ABS_GAS 0x09
#define ABS_BRAKE 0x0a
#define ABS_HAT0X 0x10
#define ABS_HAT0Y 0x11
#define ABS_HAT1X 0x12
#define ABS_HAT1Y 0x13
#define ABS_HAT2X 0x14
#define ABS_HAT2Y 0x15
#define ABS_HAT3X 0x16
#define ABS_HAT3Y 0x17

/* --- Buttons -------------------------------------------------------------
 * The BTN_ aliases that share a value (BTN_LEFT == BTN_MOUSE == BTN_0) are all
 * real: the kernel spells the same code three ways and input libraries use
 * different ones. */

#define BTN_MISC 0x100
#define BTN_0 0x100
#define BTN_1 0x101
#define BTN_2 0x102
#define BTN_3 0x103
#define BTN_4 0x104
#define BTN_5 0x105
#define BTN_6 0x106
#define BTN_7 0x107
#define BTN_8 0x108
#define BTN_9 0x109

#define BTN_MOUSE 0x110
#define BTN_LEFT 0x110
#define BTN_RIGHT 0x111
#define BTN_MIDDLE 0x112
#define BTN_SIDE 0x113
#define BTN_EXTRA 0x114
#define BTN_FORWARD 0x115
#define BTN_BACK 0x116
#define BTN_TASK 0x117

#define BTN_JOYSTICK 0x120
#define BTN_TRIGGER 0x120
#define BTN_THUMB 0x121
#define BTN_THUMB2 0x122
#define BTN_TOP 0x123
#define BTN_TOP2 0x124
#define BTN_PINKIE 0x125
#define BTN_BASE 0x126
#define BTN_BASE2 0x127
#define BTN_BASE3 0x128
#define BTN_BASE4 0x129
#define BTN_BASE5 0x12a
#define BTN_BASE6 0x12b
#define BTN_DEAD 0x12f

#define BTN_GAMEPAD 0x130
#define BTN_SOUTH 0x130
#define BTN_A 0x130
#define BTN_EAST 0x131
#define BTN_B 0x131
#define BTN_C 0x132
#define BTN_NORTH 0x133
#define BTN_X 0x133
#define BTN_WEST 0x134
#define BTN_Z 0x134
#define BTN_TL 0x136
#define BTN_TR 0x137
#define BTN_TL2 0x138
#define BTN_TR2 0x139
#define BTN_SELECT 0x13a
#define BTN_START 0x13b
#define BTN_MODE 0x13c
#define BTN_THUMBL 0x13d
#define BTN_THUMBR 0x13e

/* --- Keyboard LEDs and switches ----------------------------------------- */
#define LED_NUML 0x00
#define LED_CAPSL 0x01
#define LED_SCROLLL 0x02
#define LED_COMPOSE 0x03
#define LED_KANA 0x04

#define SW_LID 0x00
#define SW_TABLET_MODE 0x01
#define SW_HEADPHONE_INSERT 0x02

#endif  // HAVEL_PLATFORM_WINDOWS