# Hotkey Conversion Progress

## Source of Truth
All hotkeys extracted using: `grep -E "(io.Hotkey)|AddHotkey|AddContextualHotkey" src/core/HotkeyManager.cpp`

## Hotkeys to Convert

### Mode Management
- [ ] `^!g` → Mode toggle (Ctrl+Alt+G)
- [ ] `^!r` → Reload configuration (Ctrl+Alt+R)

### Application Controls
- [ ] `!Esc` → Quit application (Alt+Esc)
- [ ] `#!Esc` → Restart application (Win+Alt+Esc)

### Media Controls
- [ ] `#f1` → Playerctl previous (#F1)
- [ ] `#f2` → Playerctl play-pause (#F2)  
- [ ] `#f3` → Playerctl next (#F3)
- [ ] `f6` → Play/Pause (F6)
- [ ] `numpaddiv` → Toggle mute (Numpad Divide)

### Audio Device Controls
- [ ] `^+!a` → Audio device info (Ctrl+Shift+Alt+A)
- [ ] `^+a` → Audio device 0 (Ctrl+Shift+A)
- [ ] `^+s` → Audio device 1 (Ctrl+Shift+S)
- [ ] `^+d` → Audio device 2 (Ctrl+Shift+D)
- [ ] `^+!a` → Audio device 3 (Ctrl+Shift+Alt+A) [Duplicate]
- [ ] `^numpadsub` → Volume down (Ctrl+Numpad Subtract)
- [ ] `^numpadadd` → Volume up (Ctrl+Numpad Add)
- [ ] `^numpad0` → Volume zero (Ctrl+Numpad 0)
- [ ] `+numpadsub` → Built-in volume down (Shift+Numpad Subtract)
- [ ] `+numpadadd` → Built-in volume up (Shift+Numpad Add)
- [ ] `+numpad0` → Built-in volume zero (Shift+Numpad 0)
- [ ] `@numpadsub` → Default volume down (Alt+Numpad Subtract)
- [ ] `@numpadadd` → Default volume up (Alt+Numpad Add)

### System Controls
- [ ] `@|rwin` → Send Alt+Win+Backspace (@|RWin)
- [ ] `^!P` → Toggle CapsLock (Ctrl+Alt+P)

### Browser Controls
- [ ] `#b` → New browser window (#B)
- [ ] `#!b` → New browser tab (#Alt+B)
- [ ] `#c` → Live captions (#C)
- [ ] `#!c` → Caption script 9 en (#Alt+C)
- [ ] `#^c` → Caption script 3 auto (#Ctrl+C)
- [ ] `#+c` → Mimi script (#Shift+C)

### Mouse Events
- [ ] `~Button1` → Mouse button 1 pressed (~Button1)
- [ ] `~Button2` → Mouse button 2 pressed (~Button2)

### Zoom Controls
- [ ] `@+numpad7` → Zoom in (Alt+Shift+Numpad7)
- [ ] `@+numpad1` → Zoom out (Alt+Shift+Numpad1)
- [ ] `@+numpad5` → Zoom reset (Alt+Shift+Numpad5)

### Script Execution
- [ ] `^f1` → F1 script -1 (Ctrl+F1)
- [ ] `+f1` → F1 script 0 (Shift+F1)
- [ ] `!l` → Livelink script (Alt+L)
- [ ] `+!l` → Livelink screen toggle 1 (Shift+Alt+L)
- [ ] `!f10` → STR script (Alt+F10)
- [ ] `+!k` → Livelink screen toggle 2 (Shift+Alt+K)
- [ ] `^f10` → MPV script (Ctrl+F10)
- [ ] `!^f` → Freeze Thorium script (Alt+Ctrl+F)
- [ ] `!f` → Toggle process freeze (Alt+F)
- [ ] `!^k` → Kill process (Alt+Ctrl+K)

### Context-Sensitive Hotkeys
- [ ] `@kc89` → Zoom context-sensitive (@kc89)
- [ ] `!x` → Terminal (Alt+X) [Conditional: window.title ~ 'emacs' || window.title ~ 'alacritty']

### Debug Controls
- [ ] `$f9` → Suspend hotkeys ($F9)
- [ ] `^!t` → Switch to last window (Ctrl+Alt+T)

### Emergency Hotkeys
- [ ] `#Esc` → Restart application (#Esc)
- [ ] `^#esc` → Reload configuration (Ctrl+#Esc)

### Brightness Controls
- [ ] `f3` → Set defaults (F3)
- [ ] `+f3` → Set default temperature (Shift+F3)
- [ ] `^f3` → Set default brightness (Ctrl+F3)
- [ ] `f7` → Decrease brightness (F7)
- [ ] `^f7` → Decrease brightness display 0 (Ctrl+F7)
- [ ] `f8` → Increase brightness (F8)
- [ ] `^f8` → Increase brightness display 0 (Ctrl+F8)
- [ ] `^!f8` → Increase brightness display 1 (Ctrl+Alt+F8)
- [ ] `^!f7` → Decrease brightness display 1 (Ctrl+Alt+F7)
- [ ] `@!f7` → Decrease shadow lift (Alt+@+F7)
- [ ] `@!f8` → Increase shadow lift (Alt+@+F8)
- [ ] `@^f9` → Decrease shadow lift display 0 (Alt+Ctrl+F9)
- [ ] `@^f10` → Increase shadow lift display 0 (Alt+Ctrl+F10)
- [ ] `@^+f9` → Decrease shadow lift display 1 (Alt+Ctrl+Shift+F9)
- [ ] `@^+f10` → Increase shadow lift display 1 (Alt+Ctrl+Shift+F10)
- [ ] `@#f7` → Decrease gamma (Alt+#F7)
- [ ] `@#f8` → Increase gamma (Alt+#F8)
- [ ] `@+f9` → Decrease gamma display 0 (Alt+Shift+F9)
- [ ] `@+f10` → Increase gamma display 0 (Alt+Shift+F10)
- [ ] `@!+f10` → Increase gamma display 1 (Alt+@+Shift+F10)
- [ ] `!+f9` → Decrease gamma display 1 (Alt+Shift+F9)
- [ ] `+f7` → Decrease temperature (Shift+F7)
- [ ] `^+f7` → Decrease temperature display 0 (Ctrl+Shift+F7)
- [ ] `^!+f7` → Decrease temperature display 1 (Ctrl+Alt+Shift+F7)
- [ ] `+f8` → Increase temperature (Shift+F8)
- [ ] `^+f8` → Increase temperature display 0 (Ctrl+Shift+F8)
- [ ] `^!+f8` → Increase temperature display 1 (Ctrl+Alt+Shift+F8)

### Wheel Controls
- [ ] `~^Down` → Zoom wheel down (~Ctrl+Down)
- [ ] `~^Up` → Zoom wheel up (~Ctrl+Up)
- [ ] `@#WheelUp` → Page up (Alt+#WheelUp)
- [ ] `@#WheelDown` → Page down (Alt+#WheelDown)
- [ ] `@#!WheelUp` → Alt+Page up (Alt+#Alt+WheelUp)
- [ ] `@#!WheelDown` → Alt+Page down (Alt+#Alt+WheelDown)
- [ ] `@#^WheelDown` → Alt+9 (Alt+#Ctrl+WheelDown)
- [ ] `@#^WheelUp` → Alt+0 (Alt+#Ctrl+WheelUp)
- [ ] `@^!WheelDown` → Alt+Tab (Alt+Ctrl+Alt+WheelDown)
- [ ] `@^!WheelUp` → Alt+Shift+Tab (Alt+Ctrl+Alt+WheelUp)
- [ ] `@^+WheelUp` → Brightness up (Alt+Ctrl+Shift+WheelUp)
- [ ] `@^+WheelDown` → Brightness down (Alt+Ctrl+Shift+WheelDown)
- [ ] `@~!Tab` → Alt+Tab pressed (Alt+~Alt+Tab)
- [ ] `@~LAlt:up` → Left Alt released (Alt+~LAlt:up)
- [ ] `@~RAlt:up` → Right Alt released (Alt+~RAlt:up)
- [ ] `@!WheelUp` → Zoom or Alt+Tab (Alt+!WheelUp)
- [ ] `@!WheelDown` → Zoom or Alt+Tab (Alt+!WheelDown)
- [ ] `@RShift & WheelUp` → Zoom in (Alt+RShift & WheelUp)
- [ ] `RShift & WheelDown` → Zoom out (RShift & WheelDown)
- [ ] `@LButton & RButton:` → Zoom reset (Alt+LButton & RButton:)
- [ ] `@RButton & LButton:` → Zoom in (Alt+RButton & LButton:)
- [ ] `@RButton & WheelUp` → Zoom in (Alt+RButton & WheelUp)
- [ ] `@RButton & WheelDown` → Zoom out (Alt+RButton & WheelDown)

### Gaming Hotkeys
- [ ] `@~^l & g` → Launch Lutris (@~Ctrl+L & gaming)
- [ ] `@~^s & g` → Launch Steam (@~Ctrl+S & gaming)
- [ ] `@~^h & g` → Launch Heroic (@~Ctrl+H & gaming)

### Key Mapping
- [ ] `@+CapsLock` → CapsLock toggle (Alt+Shift+CapsLock)
- [ ] `@^CapsLock` → CapsLock toggle (Alt+Ctrl+CapsLock)
- [ ] `@!-` → Mouse sensitivity down (Alt+!-)
- [ ] `@!=` → Mouse sensitivity up (Alt+!=)

### System Applications
- [ ] `#a` → Pavucontrol (#A)

### Emergency Exit
- [ ] `@^!+#Esc` → Emergency exit (Alt+Ctrl+Alt+Shift+Esc)
- [ ] `@+#Esc` → Emergency release all keys (Alt+Shift+Esc)

### Window Management
- [ ] `@!Home` → Toggle fullscreen (Alt+!Home)
- [ ] `@^!Home` → Move to fullscreen current monitor (Alt+Ctrl+!Home)
- [ ] `!5` → Move down (Alt+5)
- [ ] `!6` → Move up (Alt+6)
- [ ] `!7` → Move left (Alt+7)
- [ ] `!8` → Move right (Alt+8)
- [ ] `!+5` → Resize down (Alt+Shift+5)
- [ ] `!+6` → Resize up (Alt+Shift+6)
- [ ] `!+7` → Resize left (Alt+Shift+7)
- [ ] `!+8` → Resize right (Alt+Shift+8)

### Clipboard
- [ ] `@!3` → Clipboard first 20000 chars (Alt+!3)
- [ ] `@!4` → Clipboard last 20000 chars (Alt+!4)

### Screenshots
- [ ] `@^Print` → Screenshot (@Ctrl+Print)
- [ ] `@Print` → Region screenshot (@Print)
- [ ] `@Pause` → Current monitor screenshot (@Pause)

### Mouse Controls
- [ ] `@|numpad5` → Left mouse hold (@|Numpad5)
- [ ] `@numpad5:up` → Left mouse release (@Numpad5:up)
- [ ] `@numpadmult` → Right mouse hold (@NumpadMult)
- [ ] `@numpadmult:up` → Right mouse release (@NumpadMult:up)
- [ ] `@numpadenter+` → Middle mouse hold (@NumpadEnter+)
- [ ] `@numpadenter:up` → Middle mouse release (@NumpadEnter:up)
- [ ] `@numpad0` → Scroll left 2 (@Numpad0)
- [ ] `@numpaddec` → Scroll right 2 (@NumpadDec)
- [ ] `@!numpad0` → Scroll right 1 (@Alt+Numpad0)
- [ ] `@+numpaddec` → Scroll down 2 (@Shift+NumpadDec)
- [ ] `@+numpad0` → Scroll up 2 (@Shift+Numpad0)
- [ ] `@numpad1` → Mouse move (-1,1) (@Numpad1)
- [ ] `@numpad2` → Mouse move (0,1) (@Numpad2)
- [ ] `@numpad3` → Mouse move (1,1) (@Numpad3)
- [ ] `@numpad4` → Mouse move (-1,0) (@Numpad4)
- [ ] `@numpad6` → Mouse move (1,0) (@Numpad6)
- [ ] `@numpad7` → Mouse move (-1,-1) (@Numpad7)
- [ ] `@numpad8` → Mouse move (0,-1) (@Numpad8)
- [ ] `@numpad9` → Mouse move (1,-1) (@Numpad9)
- [ ] `@numpad1:up` → Reset acceleration (@Numpad1:up)
- [ ] `@numpad2:up` → Reset acceleration (@Numpad2:up)
- [ ] `@numpad3:up` → Reset acceleration (@Numpad3:up)
- [ ] `@numpad4:up` → Reset acceleration (@Numpad4:up)
- [ ] `@numpad6:up` → Reset acceleration (@Numpad6:up)
- [ ] `@numpad7:up` → Reset acceleration (@Numpad7:up)
- [ ] `@numpad8:up` → Reset acceleration (@Numpad8:up)
- [ ] `@numpad9:up` → Reset acceleration (@Numpad9:up)

### Desktop Overlay
- [ ] `!d` → Toggle fake desktop overlay (Alt+D)

### Autoclicker
- [ ] `!del` → Start autoclicker (Alt+Del)

### Application Volume
- [ ] `^.` → Increase active app volume (Ctrl+.)
- [ ] `^,` → Decrease active app volume (Ctrl+,)

### Contextual Hotkeys
- [ ] `Enter` → Enter in chatterino (Conditional: window.class ~ 'chatterino')
- [ ] `h` → F spam in Genshin (Conditional: window.title ~ 'Genshin Impact')
- [ ] `~space` → Space spam in Genshin (Conditional: window.title ~ 'Genshin Impact')
- [ ] `~space:up` → Space release in Genshin (Conditional: window.title ~ 'Genshin Impact')
- [ ] `enter` → Genshin automation toggle (Conditional: window.title ~ 'Genshin Impact')
- [ ] `+s` → Skip Genshin cutscene (Conditional: window.title ~ 'Genshin Impact')
- [ ] `!+g` → Stop Genshin automation (Alt+Shift+G)

### Window Management (Separate Functions)
- [ ] `^!Up` → Move to corner 1 (Ctrl+Alt+Up)
- [ ] `^!Down` → Move to corner 2 (Ctrl+Alt+Down)
- [ ] `^!Left` → Move to corner 3 (Ctrl+Alt+Left)
- [ ] `^!Right` → Move to corner 4 (Ctrl+Alt+Right)
- [ ] `+!Up` → Resize to corner 1 (Shift+Alt+Up)
- [ ] `!+Down` → Resize to corner 2 (Alt+Shift+Down)
- [ ] `!+Left` → Resize to corner 3 (Alt+Shift+Left)
- [ ] `!+Right` → Resize to corner 4 (Alt+Shift+Right)
- [ ] `!a` → Toggle always on top (Alt+A)

### Automation
- [ ] `!delete` → Toggle autoclicker left (Alt+Delete)
- [ ] `@rshift` → Toggle autokeypresser space (Alt+RShift)

### System
- [ ] `#l` → Lock screen (#L)
- [ ] `+!Esc` → System monitor (Shift+Alt+Esc)
- [ ] `#!d` → Black overlay (#Alt+D)
- [ ] `#2` → Print active window info (#2)
- [ ] `!+i` → Toggle window focus tracking (Alt+Shift+I)
- [ ] `^!d` → Toggle verbose condition logging (Ctrl+Alt+D)

## Progress Summary
- **Total Hotkeys**: ~150+ hotkeys
- **Converted**: 0
- **Remaining**: All
- **Completion**: 0%

## Next Steps
1. Start converting hotkeys one by one
2. Test each conversion for parsing correctness
3. Add missing interpreter functions as needed
4. Commit after every 3 conversions or when adding functions