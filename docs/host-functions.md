# Host Function Reference

Complete reference for all built-in host functions available in Havel scripts.

---

## IO / Input

| Function | Args | Returns | Description |
|----------|------|---------|-------------|
| `send(key)` | string | nil | Send key sequence (XTest) |
| `io.send(key)` | string | nil | Alias for send |
| `io.sendKey(key)` | string | nil | Send single key |
| `io.sendText(text)` | string | nil | Type text string |
| `io.wait(ms)` | int | nil | Block IO thread for ms |
| `wait(ms)` | int | nil | Sleep current goroutine |
| `keyDown(key)` | string | nil | Press key down |
| `keyUp(key)` | string | nil | Release key |
| `suspend()` | - | nil | Suspend execution |

### Mouse

| Function | Args | Returns | Description |
|----------|------|---------|-------------|
| `io.click(button)` | string | nil | Click mouse button |
| `io.mouseMoveTo(x, y)` | int, int | nil | Move mouse to absolute position |
| `io.mouseMoveRel(dx, dy)` | int, int | nil | Move mouse relative |
| `io.mouseScroll(amount)` | int | nil | Scroll mouse wheel |
| `io.mouseDown(button)` | string | nil | Press mouse button |
| `io.mouseUp(button)` | string | nil | Release mouse button |
| `mouse.click(button)` | string | nil | Click mouse |
| `mouse.down(button)` | string | nil | Press mouse button |
| `mouse.up(button)` | string | nil | Release mouse button |
| `mouse.move(x, y)` | int, int | nil | Move to absolute position |
| `mouse.moveRel(dx, dy)` | int, int | nil | Move relative |
| `mouse.scroll(amount)` | int | nil | Scroll wheel |
| `mouse.pos()` | - | array | Current mouse position `[x, y]` |
| `mouse.setSpeed(speed)` | num | nil | Set mouse speed |
| `mouse.setAccel(accel)` | num | nil | Set mouse acceleration |
| `mouse.setDPI(dpi)` | int | nil | Set mouse DPI |

### Executor Mode

| Function | Args | Returns | Description |
|----------|------|---------|-------------|
| `io.getExecutorMode()` | - | string | Current executor mode |
| `io.setExecutorMode(mode)` | string | bool | Set executor mode |

---

## Window Management

### Active Window

| Function | Args | Returns | Description |
|----------|------|---------|-------------|
| `window.active()` | - | obj | Active window object |
| `active.get()` | - | obj | Alias for window.active |
| `active.title()` | - | string | Active window title |
| `active.class()` | - | string | Active window class |
| `active.exe()` | - | string | Active window executable |
| `active.pid()` | - | int | Active window PID |
| `active.close()` | - | nil | Close active window |
| `active.min()` | - | nil | Minimize active window |
| `active.max()` | - | nil | Maximize active window |
| `active.hide()` | - | nil | Hide active window |
| `active.show()` | - | nil | Show active window |
| `active.move(x, y)` | int, int | nil | Move active window |
| `active.resize(w, h)` | int, int | nil | Resize active window |

### Window Queries

| Function | Args | Returns | Description |
|----------|------|---------|-------------|
| `window.find(query)` | string | obj | Find window by title/class |
| `window.close(win)` | obj | nil | Close specific window |
| `window.resize(win, w, h)` | obj, int, int | nil | Resize window |
| `window.move(win, x, y)` | obj, int, int | nil | Move window |
| `window.focus(win)` | obj | nil | Focus window |
| `window.min(win)` | obj | nil | Minimize window |
| `window.max(win)` | obj | nil | Maximize window |
| `window.hide(win)` | obj | nil | Hide window |
| `window.show(win)` | obj | nil | Show window |
| `window.restore(win)` | obj | nil | Restore window |
| `window.center(win)` | obj | nil | Center window |
| `window.snap(win, dir)` | obj, string | nil | Snap to edge |
| `window.fullscreen(win)` | obj | nil | Toggle fullscreen |
| `window.moveResize(win)` | obj | nil | Start move-resize |
| `window.setAlwaysOnTop(win, bool)` | obj, bool | nil | Pin/unpin window |
| `window.moveToMonitor(win, mon)` | obj, int | nil | Move to monitor |
| `window.moveToNextMonitor(win)` | obj | nil | Move to next monitor |

### Window Enumeration

| Function | Args | Returns | Description |
|----------|------|---------|-------------|
| `window.any(query)` | string | bool | True if any window matches |
| `window.count()` | - | int | Number of windows |
| `window.filter(query)` | string | array | Filter windows by criteria |
| `window.list()` | - | array | All windows |
| `window.each(callback)` | fn | nil | Iterate over windows |
| `window.sort(key)` | string | array | Sort windows |
| `window.map(callback)` | fn | array | Map windows |
| `window.title(win)` | obj | string | Get window title |
| `window.class(win)` | obj | string | Get window class |
| `window.exe(win)` | obj | string | Get window exe |
| `window.pid(win)` | obj | int | Get window PID |
| `window.id(win)` | obj | int | Get window ID |
| `window.area(win)` | obj | obj | Get window geometry |
| `window.pos(win)` | obj | obj | Get window position |

### Window Monitor

| Function | Args | Returns | Description |
|----------|------|---------|-------------|
| `window.cmd(win)` | obj | string | Get window command |

---

## Display

| Function | Args | Returns | Description |
|----------|------|---------|-------------|
| `display.displayNum()` | - | int | Number of displays |
| `display.getCount()` | - | int | Number of displays |
| `display.getMonitors()` | - | array | Monitor info list |
| `display.getMonitorsArea()` | - | array | Monitor area info |
| `display.getPrimary()` | - | int | Primary monitor index |
| `display.isWayland()` | - | bool | Running on Wayland |
| `display.isX11()` | - | bool | Running on X11 |
| `display.isWindows()` | - | bool | Running on Windows |
| `display.protocol()` | - | string | Display protocol name |
| `display.wm()` | - | string | Window manager name |
| `display.monitorsResolution()` | - | array | Monitor resolutions |

---

## Process

| Function | Args | Returns | Description |
|----------|------|---------|-------------|
| `process.run(cmd)` | string | obj | Run command, wait for completion. Returns `{stdout, stderr, exitCode, success}` |
| `process.runDetached(cmd)` | string | int | Run command async, returns PID |
| `process.find(name)` | string | array | Find processes by name |
| `process.exists(name)` | string | bool | Check if process exists |
| `process.kill(pid)` | int | bool | Kill process by PID |
| `process.nice(pid, level)` | int, int | bool | Set process niceness |
| `run(cmd)` | string | obj | Alias for process.run |
| `runCapture(cmd)` | string | obj | Run and capture output |
| `runDetached(cmd)` | string | int | Alias for process.runDetached |
| `execute(cmd)` | string | obj | Execute command |
| `getpid()` | - | int | Current process PID |
| `getppid()` | - | int | Parent process PID |

---

## File I/O

| Function | Args | Returns | Description |
|----------|------|---------|-------------|
| `readFile(path)` | string | string | Read file contents |
| `writeFile(path, content)` | string, string | nil | Write file contents |
| `fileExists(path)` | string | bool | Check if file exists |
| `fileSize(path)` | string | int | Get file size in bytes |
| `deleteFile(path)` | string | bool | Delete a file |

---

## Hotkey

| Function | Args | Returns | Description |
|----------|------|---------|-------------|
| `hotkey.register(key, action)` | string, fn | bool | Register a hotkey |
| `hotkey.register_conditional(key, action, condition)` | string, fn, fn | bool | Register conditional hotkey |
| `hotkey.remove_conditional(id)` | int | bool | Remove conditional hotkey |
| `hotkey.enable_conditional(id)` | int | bool | Enable conditional monitoring |
| `hotkey.disable_conditional(id)` | int | bool | Disable conditional monitoring |
| `hotkey.set_condition(id, expr)` | int, string | bool | Update condition expression |
| `hotkey.evaluate_condition(id)` | int | bool or nil | Evaluate condition now |
| `hotkey.conditional_list()` | - | array | List all conditional hotkeys |
| `hotkey.trigger(alias)` | string | bool | Programmatically trigger hotkey |
| `hotkey.list()` | - | array | List all registered hotkeys |

See [hotkey-system.md](hotkey-system.md) for the full Hotkey object API.

---

## Mode

| Function | Args | Returns | Description |
|----------|------|---------|-------------|
| `mode()` | - | string | Current mode name (shorthand) |
| `mode.current()` | - | string | Current mode name |
| `mode.previous()` | - | string | Previous mode name |
| `mode.set(name)` | string | bool | Set mode, triggers hotkey re-evaluation |
| `mode.register(...)` | 9 args | bool | Define a mode |

See [mode-system.md](mode-system.md) for full details.

---

## Timer

| Function | Args | Returns | Description |
|----------|------|---------|-------------|
| `timer.after(ms, callback)` | int, fn | int | Fire callback after ms, returns timer ID |
| `timer.every(ms, callback)` | int, fn | int | Fire callback every ms, returns timer ID |

---

## Clipboard

| Function | Args | Returns | Description |
|----------|------|---------|-------------|
| `clipboard.get()` | - | string | Get clipboard text |
| `clipboard.set(text)` | string | nil | Set clipboard text |
| `clipboard.clear()` | - | nil | Clear clipboard |

---

## Audio

| Function | Args | Returns | Description |
|----------|------|---------|-------------|
| `audio.getVolume()` | - | num | Get master volume (0-100) |
| `audio.setVolume(level)` | num | nil | Set master volume |
| `audio.increaseVolume(step)` | num | nil | Increase volume |
| `audio.decreaseVolume(step)` | num | nil | Decrease volume |
| `audio.toggleMute()` | - | nil | Toggle mute |
| `audio.isMuted()` | - | bool | Check if muted |
| `audio.setMute(bool)` | bool | nil | Set mute state |
| `audio.getDevices()` | - | array | List audio devices |
| `audio.findDeviceByIndex(idx)` | int | obj | Find device by index |
| `audio.findDeviceByName(name)` | string | obj | Find device by name |
| `audio.getDefaultOutput()` | - | obj | Get default output device |
| `audio.setDefaultOutput(dev)` | obj | nil | Set default output |
| `audio.playTestSound()` | - | nil | Play test sound |

---

## Media / MPV

| Function | Args | Returns | Description |
|----------|------|---------|-------------|
| `media.getActivePlayer()` | - | string | Active media player |
| `media.getAvailablePlayers()` | - | array | Available players |
| `media.getVolume()` | - | num | Media volume |
| `media.setVolume(level)` | num | nil | Set media volume |
| `media.play()` | - | nil | Play |
| `media.pause()` | - | nil | Pause |
| `media.playPause()` | - | nil | Toggle play/pause |
| `media.stop()` | - | nil | Stop |
| `media.next()` | - | nil | Next track |
| `media.previous()` | - | nil | Previous track |
| `mpv.cmd(command)` | string | obj | Send MPV command |
| `mpv.seek(seconds)` | num | nil | Seek in current file |
| `mpv.next()` | - | nil | Next in playlist |
| `mpv.previous()` | - | nil | Previous in playlist |
| `mpv.stop()` | - | nil | Stop playback |
| `mpv.volumeUp(step)` | num | nil | Volume up |
| `mpv.volumeDown(step)` | num | nil | Volume down |
| `mpv.toggleMute()` | - | nil | Toggle mute |
| `mpv.addSpeed(delta)` | num | nil | Change playback speed |
| `mpv.screenshot()` | - | nil | Take screenshot |
| `mpv.addSubDelay(ms)` | int | nil | Adjust subtitle delay |
| `mpv.addSubScale(delta)` | num | nil | Adjust subtitle scale |
| `mpv.subSeek(offset)` | int | nil | Seek to subtitle |
| `mpv.copySubtitle()` | - | string | Copy current subtitle text |
| `mpv.cycle(property)` | string | nil | Cycle a property |
| `mpv.ipcSet(path)` | string | nil | Set IPC socket path |
| `mpv.ipcReset()` | - | nil | Reset IPC connection |

---

## Screenshot

| Function | Args | Returns | Description |
|----------|------|---------|-------------|
| `screenshot.full()` | - | obj | Full screenshot, returns `{path, data, width, height}` |
| `screenshot.monitor(idx)` | int | obj | Screenshot specific monitor |

---

## Config

| Function | Args | Returns | Description |
|----------|------|---------|-------------|
| `config.get(key)` | string | string | Get config value |
| `config.set(key, value, debounce)` | string, string, bool | nil | Set config value (debounced save) |
| `config.save()` | - | nil | Force save config |

---

## System / App

| Function | Args | Returns | Description |
|----------|------|---------|-------------|
| `system.detect()` | - | obj | Detect system info |
| `system.hardware()` | - | obj | Hardware info |
| `app.getName()` | - | string | Application name |
| `app.getVersion()` | - | string | Application version |
| `app.getOS()` | - | string | OS name |
| `app.getHostname()` | - | string | Hostname |
| `app.getUsername()` | - | string | Username |
| `app.getHomeDir()` | - | string | Home directory |
| `app.getEnv(key)` | string | string | Get environment variable |
| `app.setEnv(key, value)` | string, string | nil | Set environment variable |
| `app.getCpuCores()` | - | int | CPU core count |
| `app.openUrl(url)` | string | nil | Open URL in browser |
| `app.exit()` | - | nil | Exit application |
| `app.restart()` | - | nil | Restart application |

---

## Alt-Tab

| Function | Args | Returns | Description |
|----------|------|---------|-------------|
| `alttab.next()` | - | nil | Switch to next window |
| `alttab.previous()` | - | nil | Switch to previous window |
| `alttab.show()` | - | nil | Show alt-tab overlay |
| `alttab.hide()` | - | nil | Hide alt-tab overlay |
| `alttab.toggle()` | - | nil | Toggle alt-tab overlay |
| `alttab.select()` | - | nil | Select current window |
| `alttab.getWindows()` | - | array | Get window list |

---

## GUI

| Function | Args | Returns | Description |
|----------|------|---------|-------------|
| `gui.notify(title, text)` | string, string | nil | Show desktop notification |

---

## Map Manager

| Function | Args | Returns | Description |
|----------|------|---------|-------------|
| `mapmanager.getCurrentProfile()` | - | string | Current input profile |

---

## Window Groups

| Function | Args | Returns | Description |
|----------|------|---------|-------------|
| `group.add(name, windows)` | string, array | bool | Create window group |
| `group.find(name)` | string | obj | Find group by name |
| `group.findBy(window)` | obj | obj | Find group containing window |
| `group.get(name)` | string | obj | Get group |
| `group.list()` | - | array | List all groups |
| `group.remove(name)` | string | bool | Remove group |

---

## Automation

| Function | Args | Returns | Description |
|----------|------|---------|-------------|
| `automation.createAutoClicker(opts)` | obj | int | Create auto-clicker task |
| `automation.createAutoKeyPresser(opts)` | obj | int | Create auto-keypress task |
| `automation.createAutoRunner(opts)` | obj | int | Create auto-runner task |
| `automation.hasTask(id)` | int | bool | Check if task exists |
| `automation.removeTask(id)` | int | bool | Remove task |
| `automation.stopAll()` | - | nil | Stop all automation tasks |

---

## Text Chunker

| Function | Args | Returns | Description |
|----------|------|---------|-------------|
| `textchunker.setText(text)` | string | nil | Set chunk text |
| `textchunker.setChunkSize(size)` | int | nil | Set chunk size |
| `textchunker.getText()` | - | string | Get full text |
| `textchunker.getChunk(idx)` | int | string | Get chunk by index |
| `textchunker.getCurrentChunk()` | - | string | Get current chunk |
| `textchunker.getNextChunk()` | - | string | Get next chunk |
| `textchunker.getPreviousChunk()` | - | string | Get previous chunk |
| `textchunker.getTotalChunks()` | - | int | Total chunk count |
| `textchunker.goToFirst()` | - | nil | Go to first chunk |
| `textchunker.goToLast()` | - | nil | Go to last chunk |
| `textchunker.setCurrentChunk(idx)` | int | nil | Set current chunk index |
| `textchunker.clear()` | - | nil | Clear all text |

---

## Play

| Function | Args | Returns | Description |
|----------|------|---------|-------------|
| `play(path)` | string | nil | Play audio file |
