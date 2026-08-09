---
title: "Host Functions Reference"
description: "Complete reference of all built-in host functions with signatures and descriptions."
---

# Host Functions Reference

All built-in functions exposed to Havel scripts via the host module system.

---

## Core Functions

| Function | Module | Signature | Description |
|----------|--------|-----------|-------------|
| `print` | builtin | `(value: any) -> nil` | Print to stdout |
| `help` | builtin | `(topic?: str) -> str` | Interactive help |
| `type` | builtin | `(value: any) -> str` | Get type name |
| `len` | builtin | `(value: any) -> int` | Length of collection |
| `approx` | builtin | `(a: num, b: num) -> bool` | Fuzzy float comparison |
| `sleep` | builtin | `(ms: int) -> nil` | Sleep current goroutine |
| `wait` | builtin | `(ms: int) -> nil` | Alias for sleep |

---

## Math Module (`math`)

| Function | Signature | Description |
|----------|-----------|-------------|
| `math.abs` | `(x: num) -> num` | Absolute value |
| `math.sqrt` | `(x: num) -> num` | Square root |
| `math.pow` | `(x: num, y: num) -> num` | Power |
| `math.sin` | `(x: num) -> num` | Sine (radians) |
| `math.cos` | `(x: num) -> num` | Cosine (radians) |
| `math.tan` | `(x: num) -> num` | Tangent (radians) |
| `math.asin` | `(x: num) -> num` | Arc sine |
| `math.acos` | `(x: num) -> num` | Arc cosine |
| `math.atan` | `(x: num) -> num` | Arc tangent |
| `math.atan2` | `(y: num, x: num) -> num` | Two-argument arc tangent |
| `math.ceil` | `(x: num) -> int` | Ceiling |
| `math.floor` | `(x: num) -> int` | Floor |
| `math.round` | `(x: num) -> int` | Round to nearest |
| `math.log` | `(x: num) -> num` | Natural logarithm |
| `math.log10` | `(x: num) -> num` | Base-10 logarithm |
| `math.log2` | `(x: num) -> num` | Base-2 logarithm |
| `math.exp` | `(x: num) -> num` | Exponential (e^x) |
| `math.min` | `(a: num, b: num) -> num` | Minimum |
| `math.max` | `(a: num, b: num) -> num` | Maximum |
| `math.clamp` | `(x: num, lo: num, hi: num) -> num` | Clamp to range |
| `math.random` | `() -> num` | Random float [0, 1) |
| `math.randomInt` | `(lo: int, hi: int) -> int` | Random integer in range |
| `math.sign` | `(x: num) -> int` | Sign (-1, 0, 1) |
| `math.degrees` | `(x: num) -> num` | Radians to degrees |
| `math.radians` | `(x: num) -> num` | Degrees to radians |
| `math.lerp` | `(a: num, b: num, t: num) -> num` | Linear interpolation |
| `math.isNaN` | `(x: num) -> bool` | Check for NaN |
| `math.isInf` | `(x: num) -> bool` | Check for infinity |

Constants: `math.pi`, `math.e`, `math.inf`

---

## String Module (`str`)

| Function | Signature | Description |
|----------|-----------|-------------|
| `str.len` | `(s: str) -> int` | String length |
| `str.upper` | `(s: str) -> str` | Uppercase |
| `str.lower` | `(s: str) -> str` | Lowercase |
| `str.trim` | `(s: str) -> str` | Trim whitespace |
| `str.ltrim` | `(s: str) -> str` | Trim leading |
| `str.rtrim` | `(s: str) -> str` | Trim trailing |
| `str.split` | `(s: str, delim: str) -> array` | Split by delimiter |
| `str.join` | `(arr: array, delim: str) -> str` | Join array |
| `str.has` | `(s: str, sub: str) -> bool` | Contains substring |
| `str.startsWith` | `(s: str, prefix: str) -> bool` | Starts with prefix |
| `str.endsWith` | `(s: str, suffix: str) -> bool` | Ends with suffix |
| `str.replace` | `(s: str, old: str, new: str) -> str` | Replace all |
| `str.sub` | `(s: str, start: int, len: int) -> str` | Substring |
| `str.reverse` | `(s: str) -> str` | Reverse string |
| `str.repeat` | `(s: str, n: int) -> str` | Repeat n times |
| `str.char` | `(s: str, idx: int) -> str` | Character at index |
| `str.ord` | `(s: str) -> int` | Unicode code point |
| `str.chr` | `(n: int) -> str` | Character from code point |
| `str.padLeft` | `(s: str, len: int, pad: str) -> str` | Left-pad |
| `str.padRight` | `(s: str, len: int, pad: str) -> str` | Right-pad |
| `str.toInt` | `(s: str) -> int` | Parse integer |
| `str.toNum` | `(s: str) -> num` | Parse float |
| `str.format` | `(fmt: str, args...) -> str` | Format string |

---

## Array Module (`arr`)

| Function | Signature | Description |
|----------|-----------|-------------|
| `arr.len` | `(a: array) -> int` | Array length |
| `arr.push` | `(a: array, val: any) -> nil` | Append element |
| `arr.pop` | `(a: array) -> any` | Remove and return last |
| `arr.insert` | `(a: array, idx: int, val: any) -> nil` | Insert at index |
| `arr.remove` | `(a: array, idx: int) -> any` | Remove at index |
| `arr.has` | `(a: array, val: any) -> bool` | Contains element |
| `arr.indexOf` | `(a: array, val: any) -> int` | First index (-1 if not found) |
| `arr.map` | `(a: array, fn: fn) -> array` | Transform each |
| `arr.filter` | `(a: array, fn: fn) -> array` | Keep matching |
| `arr.reduce` | `(a: array, fn: fn, init: any) -> any` | Reduce to single value |
| `arr.forEach` | `(a: array, fn: fn) -> nil` | Iterate with side effects |
| `arr.sort` | `(a: array) -> array` | Sort ascending |
| `arr.sortBy` | `(a: array, fn: fn) -> array` | Sort with comparator |
| `arr.reverse` | `(a: array) -> array` | Reverse in place |
| `arr.slice` | `(a: array, start: int, end: int) -> array` | Sub-array |
| `arr.concat` | `(a: array, b: array) -> array` | Concatenate |
| `arr.flat` | `(a: array) -> array` | Flatten one level |
| `arr.unique` | `(a: array) -> array` | Remove duplicates |
| `arr.first` | `(a: array) -> any` | First element |
| `arr.last` | `(a: array) -> any` | Last element |
| `arr.min` | `(a: array) -> any` | Minimum element |
| `arr.max` | `(a: array) -> any` | Maximum element |
| `arr.sum` | `(a: array) -> num` | Sum of elements |
| `arr.join` | `(a: array, delim: str) -> str` | Join as string |

---

## Object Module (`obj`)

| Function | Signature | Description |
|----------|-----------|-------------|
| `obj.keys` | `(o: object) -> array` | Get all keys |
| `obj.values` | `(o: object) -> array` | Get all values |
| `obj.has` | `(o: object, key: str) -> bool` | Has key |
| `obj.remove` | `(o: object, key: str) -> any` | Remove key |
| `obj.merge` | `(a: object, b: object) -> object` | Merge two objects |
| `obj.clone` | `(o: object) -> object` | Shallow copy |
| `obj.len` | `(o: object) -> int` | Number of keys |
| `obj.forEach` | `(o: object, fn: fn) -> nil` | Iterate key-value |
| `obj.map` | `(o: object, fn: fn) -> object` | Transform values |
| `obj.filter` | `(o: object, fn: fn) -> object` | Filter key-value |
| `obj.toJson` | `(o: object) -> str` | Serialize to JSON |
| `obj.fromJson` | `(s: str) -> object` | Parse from JSON |

---

## Type Module (`type`)

| Function | Signature | Description |
|----------|-----------|-------------|
| `type` | `(val: any) -> str` | Type name string |
| `type.isInt` | `(val: any) -> bool` | Check if integer |
| `type.isNum` | `(val: any) -> bool` | Check if float |
| `type.isStr` | `(val: any) -> bool` | Check if string |
| `type.isBool` | `(val: any) -> bool` | Check if boolean |
| `type.isNil` | `(val: any) -> bool` | Check if nil |
| `type.isArray` | `(val: any) -> bool` | Check if array |
| `type.isObject` | `(val: any) -> bool` | Check if object |
| `type.isFn` | `(val: any) -> bool` | Check if function |
| `type.isClass` | `(val: any) -> bool` | Check if class prototype |
| `type.implements` | `(val: any, trait: str) -> bool` | Check trait conformance |

---

## FS Module (`fs`)

| Function | Signature | Description |
|----------|-----------|-------------|
| `fs.read` | `(path: str) -> str` | Read file contents |
| `fs.write` | `(path: str, content: str) -> nil` | Write file |
| `fs.append` | `(path: str, content: str) -> nil` | Append to file |
| `fs.exists` | `(path: str) -> bool` | File exists |
| `fs.size` | `(path: str) -> int` | File size in bytes |
| `fs.delete` | `(path: str) -> bool` | Delete file |
| `fs.rename` | `(old: str, new: str) -> bool` | Rename file |
| `fs.copy` | `(src: str, dst: str) -> bool` | Copy file |
| `fs.move` | `(src: str, dst: str) -> bool` | Move file |
| `fs.mkdir` | `(path: str) -> bool` | Create directory |
| `fs.readdir` | `(path: str) -> array` | List directory |
| `fs.isdir` | `(path: str) -> bool` | Is directory |
| `fs.isfile` | `(path: str) -> bool` | Is regular file |
| `fs.cwd` | `() -> str` | Current working directory |
| `fs.abspath` | `(path: str) -> str` | Absolute path |
| `fs.basename` | `(path: str) -> str` | File name component |
| `fs.dirname` | `(path: str) -> str` | Directory component |
| `fs.ext` | `(path: str) -> str` | File extension |
| `fs.stem` | `(path: str) -> str` | File name without extension |

---

## Process Module (`process`)

| Function | Signature | Description |
|----------|-----------|-------------|
| `process.spawn` | `(cmd: str, args?: array) -> int` | Spawn process, return PID |
| `process.exec` | `(cmd: str, args?: array) -> object` | Execute, return {stdout, stderr, exitCode} |
| `process.env` | `(key: str) -> str` | Get environment variable |
| `process.setEnv` | `(key: str, val: str) -> nil` | Set environment variable |
| `process.unsetEnv` | `(key: str) -> nil` | Unset environment variable |
| `process.cwd` | `() -> str` | Current working directory |
| `process.chdir` | `(path: str) -> bool` | Change directory |
| `process.exit` | `(code: int) -> nil` | Exit process |
| `process.pid` | `() -> int` | Current PID |
| `process.ppid` | `() -> int` | Parent PID |
| `process.args` | `() -> array` | Command line arguments |

---

## Sys Module (`sys`)

| Function | Signature | Description |
|----------|-----------|-------------|
| `sys.detect` | `() -> object` | System info (OS, arch, kernel) |
| `sys.hardware` | `() -> object` | Hardware info (CPU, memory) |
| `sys.time` | `() -> num` | Unix timestamp (seconds) |
| `sys.timeMs` | `() -> int` | Unix timestamp (ms) |
| `sys.clock` | `() -> num` | Monotonic clock (seconds) |
| `sys.sleep` | `(ms: int) -> nil` | Sleep current thread |
| `sys.exit` | `(code: int) -> nil` | Exit process |
| `sys.platform` | `() -> str` | "linux", "windows", "macos" |
| `sys.arch` | `() -> str` | "x86_64", "aarch64" |
| `sys.hostname` | `() -> str` | Hostname |
| `sys.user` | `() -> str` | Username |
| `sys.uptime` | `() -> num` | Uptime in seconds |
| `sys.memory` | `() -> object` | Memory info |
| `sys.cpu` | `() -> object` | CPU info |

---

## Time Module (`time`)

| Function | Signature | Description |
|----------|-----------|-------------|
| `time.now` | `() -> num` | Current Unix timestamp |
| `time.epoch` | `() -> int` | Current Unix timestamp (ms) |
| `time.sleep` | `(ms: int) -> nil` | Sleep |
| `time.duration` | `(ms: int) -> str` | Human-readable duration |
| `time.format` | `(ts: num, fmt: str) -> str` | Format timestamp |
| `time.parse` | `(str: str, fmt: str) -> num` | Parse time string |
| `time.year` | `(ts: num) -> int` | Extract year |
| `time.month` | `(ts: num) -> int` | Extract month (1-12) |
| `time.day` | `(ts: num) -> int` | Extract day (1-31) |
| `time.hour` | `(ts: num) -> int` | Extract hour (0-23) |
| `time.minute` | `(ts: num) -> int` | Extract minute (0-59) |
| `time.second` | `(ts: num) -> int` | Extract second (0-59) |

---

## HTTP Module (`http`)

| Function | Signature | Description |
|----------|-----------|-------------|
| `http.get` | `(url: str) -> object` | HTTP GET |
| `http.post` | `(url: str, body: str) -> object` | HTTP POST |
| `http.put` | `(url: str, body: str) -> object` | HTTP PUT |
| `http.delete` | `(url: str) -> object` | HTTP DELETE |

Response: `{ statusCode, headers, body, text, json }`

---

## Network Module (`net`)

| Function | Signature | Description |
|----------|-----------|-------------|
| `net.tcp.connect` | `(host: str, port: int) -> socket` | TCP connect |
| `net.tcp.listen` | `(port: int, handler: fn) -> server` | TCP listen |
| `net.udp.bind` | `(port: int) -> socket` | UDP bind |
| `net.udp.send` | `(sock, host, port, data) -> int` | UDP send |
| `net.udp.recv` | `(sock, size) -> {data, host, port}` | UDP receive |
| `net.ws.connect` | `(url: str) -> ws` | WebSocket connect |
| `net.ws.listen` | `(port: int, handler: fn) -> server` | WebSocket listen |

Socket: `send`, `recv`, `recvLine`, `close`, `setTimeout`
WebSocket: `send`, `recv`, `onMessage`, `onClose`, `close`

---

## Clipboard Module (`clipboard`)

| Function | Signature | Description |
|----------|-----------|-------------|
| `clipboard.get` | `() -> str` | Get clipboard text |
| `clipboard.set` | `(text: str) -> nil` | Set clipboard text |
| `clipboard.watch` | `(callback: fn) -> int` | Watch for changes |
| `clipboard.unwatch` | `(id: int) -> nil` | Stop watching |
| `clipboard.history` | `() -> array` | Get history |
| `clipboard.historyLimit` | `(n: int) -> nil` | Set history limit |
| `clipboard.clearHistory` | `() -> nil` | Clear history |

---

## Window Module (`window`)

| Function | Signature | Description |
|----------|-----------|-------------|
| `window.active` | `() -> object` | Active window |
| `window.activeId` | `() -> int` | Active window ID |
| `window.list` | `() -> array` | All windows |
| `window.listVisible` | `() -> array` | Visible windows |
| `window.focus` | `(id: int) -> bool` | Focus window |
| `window.raise` | `(id: int) -> bool` | Raise window |
| `window.move` | `(id: int, x: int, y: int) -> bool` | Move window |
| `window.resize` | `(id: int, w: int, h: int) -> bool` | Resize window |
| `window.moveResize` | `(id: int, x: int, y: int, w: int, h: int) -> bool` | Move and resize |
| `window.title` | `(id: int) -> str` | Window title |
| `window.class` | `(id: int) -> str` | Window class |
| `window.pid` | `(id: int) -> int` | Window PID |
| `window.geometry` | `(id: int) -> object` | { x, y, w, h } |
| `window.state` | `(id: int) -> str` | Window state |
| `window.close` | `(id: int) -> bool` | Close window |
| `window.minimize` | `(id: int) -> bool` | Minimize |
| `window.maximize` | `(id: int) -> bool` | Maximize |
| `window.fullscreen` | `(id: int, enable: bool) -> bool` | Fullscreen |
| `window.kill` | `(id: int) -> bool` | Force kill |
| `window.find` | `(criteria: object) -> array` | Find windows |
| `window.findOne` | `(criteria: object) -> object/nil` | Find one window |
| `window.monitors` | `() -> array` | Monitor info |
| `window.monitorOf` | `(id: int) -> object` | Monitor of window |

---

## Brightness Module (`brightness`)

| Function | Signature | Description |
|----------|-----------|-------------|
| `brightness.get` | `() -> num` | Get brightness (0-1) |
| `brightness.set` | `(value: num) -> nil` | Set brightness (0-1) |
| `brightness.increase` | `(delta: num) -> nil` | Increase brightness |
| `brightness.decrease` | `(delta: num) -> nil` | Decrease brightness |
| `brightness.temperature` | `() -> int` | Get color temperature (K) |
| `brightness.setTemperature` | `(kelvin: int) -> nil` | Set color temperature |
| `brightness.gamma` | `() -> num` | Get gamma |
| `brightness.setGamma` | `(value: num) -> nil` | Set gamma |
| `brightness.getMonitor` | `(name: str) -> num` | Monitor brightness |
| `brightness.setMonitor` | `(name: str, value: num) -> nil` | Set monitor brightness |
| `brightness.presets` | `() -> array` | Available presets |
| `brightness.applyPreset` | `(name: str) -> nil` | Apply preset |
| `brightness.step` | `() -> num` | Get step size |
| `brightness.setStep` | `(value: num) -> nil` | Set step size |

---

## Hotkey Module (`hotkey`)

| Function | Signature | Description |
|----------|-----------|-------------|
| `hotkey.register` | `(key, action, policy?, alias?) -> bool` | Register hotkey |
| `hotkey.register_conditional` | `(key, action, condition, alias?) -> bool` | Conditional register |
| `hotkey.list` | `() -> array` | List all hotkeys |
| `hotkey.enabled` | `(key: str) -> bool` | Check if enabled |
| `hotkey.grab` | `(key: str) -> bool` | Grab key |
| `hotkey.ungrab` | `(key: str) -> bool` | Ungrab key |
| `hotkey.unregister` | `(key: str) -> bool` | Unregister |
| `hotkey.unregisterByAlias` | `(alias: str) -> bool` | Unregister by alias |
| `hotkey.removeAll` | `() -> int` | Remove all |
| `hotkey.clearAll` | `() -> int` | Clear all (alias) |

Conditional:
| Function | Signature | Description |
|----------|-----------|-------------|
| `hotkey.remove_conditional` | `(id: int) -> bool` | Remove conditional |
| `hotkey.enable_conditional` | `(id: int) -> bool` | Enable conditional |
| `hotkey.disable_conditional` | `(id: int) -> bool` | Disable conditional |
| `hotkey.set_condition` | `(id: int, expr: str) -> bool` | Update condition |
| `hotkey.evaluate_condition` | `(id: int) -> bool` | Evaluate now |
| `hotkey.conditional_list` | `() -> array` | List conditionals |

---

**Previous:** [Migration Guide](/guides/migration)
**Next:** [FFI Reference →](/reference/ffi)