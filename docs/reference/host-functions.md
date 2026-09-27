---
title: "Host Functions Reference"
description: "Reference of built-in host functions, verified against the live module registry."
---

# Host Functions Reference

Built-in functions exposed to Havel scripts. Every name on this page was
verified against the live module registry (`object.keys(module)` dumps from
a running VM). Names prefixed with `_` are internal — scripts should not
call them.

Builtins are plain globals, not module members. Module members are accessed
as `mod.member`.

---

## Core builtins

| Name | Signature | Description |
|------|-----------|-------------|
| `print` | `(value: any) -> nil` | Print to stdout |
| `help` | `(topic?: str) -> str` | Interactive help |
| `type` | `(value: any) -> str` | Type name string |
| `len` | `(value: any) -> int` | Length of collection |
| `sleep` | `(ms: int) -> nil` | Sleep current thread |
| `wait` | `(ms: int) -> nil` | Sleep current thread |

---

## Math (`math`)

| Name | Signature | Description |
|------|-----------|-------------|
| `math.abs` | `(x: num) -> num` | Absolute value |
| `math.sqrt` | `(x: num) -> num` | Square root |
| `math.cbrt` | `(x: num) -> num` | Cube root |
| `math.pow` | `(x: num, y: num) -> num` | Power |
| `math.exp` | `(x: num) -> num` | Exponential (e^x) |
| `math.log` | `(x: num) -> num` | Natural logarithm |
| `math.log10` | `(x: num) -> num` | Base-10 logarithm |
| `math.log2` | `(x: num) -> num` | Base-2 logarithm |
| `math.sin` | `(x: num) -> num` | Sine (radians) |
| `math.cos` | `(x: num) -> num` | Cosine (radians) |
| `math.tan` | `(x: num) -> num` | Tangent (radians) |
| `math.asin` | `(x: num) -> num` | Arc sine |
| `math.acos` | `(x: num) -> num` | Arc cosine |
| `math.atan2` | `(y: num, x: num) -> num` | Two-argument arc tangent |
| `math.ceil` | `(x: num) -> int` | Ceiling |
| `math.floor` | `(x: num) -> int` | Floor |
| `math.round` | `(x: num) -> int` | Round to nearest |
| `math.fract` | `(x: num) -> num` | Fractional part |
| `math.rem` | `(a: num, b: num) -> num` | Remainder |
| `math.mod` | `(a: num, b: num) -> num` | Modulo |
| `math.min` | `(...) -> num` | Minimum of arguments |
| `math.max` | `(...) -> num` | Maximum of arguments |
| `math.mean` | `(...) -> num` | Mean of arguments |
| `math.sum` | `(...) -> num` | Sum of arguments |
| `math.clamp` | `(x: num, lo: num, hi: num) -> num` | Clamp to range |
| `math.lerp` | `(a: num, b: num, t: num) -> num` | Linear interpolation |
| `math.sign` | `(x: num) -> int` | Sign (-1, 0, 1) |
| `math.copysign` | `(x: num, y: num) -> num` | Copy sign of y onto x |
| `math.hypot` | `(a: num, b: num) -> num` | Hypotenuse length |
| `math.fma` | `(a: num, b: num, c: num) -> num` | Fused multiply-add |
| `math.distance` | `(...) -> num` | Euclidean distance |
| `math.random` | `() -> num` | Random float [0, 1) |
| `math.random_range` | `(lo: num, hi: num) -> num` | Random float in range |
| `math.randint` | `(lo: int, hi: int) -> int` | Random integer in range |
| `math.choice` | `(arr: array) -> any` | Random element of array |
| `math.deg2rad` | `(x: num) -> num` | Degrees to radians |
| `math.rad2deg` | `(x: num) -> num` | Radians to degrees |
| `math.is_nan` | `(x: num) -> bool` | Check for NaN |
| `math.is_inf` | `(x: num) -> bool` | Check for infinity |
| `math.is_finite` | `(x: num) -> bool` | Check for finite value |

### Constants

Verified values:

| Constant | Value | Description |
|----------|-------|-------------|
| `math.PI` | 3.14159 | Pi |
| `math.E` | 2.71828 | Euler's number |
| `math.INF` | inf | Infinity |
| `math.NAN` | null | Not a number |
| `math.TAU` | 6.28319 | Tau (2π) |
| `math.SQRT2` | 1.41421 | Square root of 2 |
| `math.C` | 2.99792e+08 | Speed of light (m/s) |
| `math.G` | 9.80665 | Standard gravity (m/s²) |
| `math.h` | 6.62607e-34 | Planck constant (J·s) |
| `math.KB` | 1.38065e-23 | Boltzmann constant (J/K) |
| `math.NA` | 6.02214e+23 | Avogadro number (1/mol) |
| `math.ECHARGE` | 1.60218e-19 | Elementary charge (C) |

More constants exist (case-aliased pairs like `me`/`ME`, `mp`/`MP`, `mu0`/`MU0`,
`eps0`/`EPS0`, `sigma`/`SIGMA`, plus `HPLANCK`, `RGAS`, `RINF`, `a0`, `A0`,
`re`, `u`, `k`, `G0`, `EV`, `eV`, `c`). Physics helper functions also exist:
`kinetic_energy`, `potential_energy`, `photon_energy`, `thermal_energy`,
`relativistic_mass`, `momentum`, `force`, `coulomb_force`,
`gravitational_force`, `lorentz_factor`, `escape_velocity`,
`schwarzschild_radius`, `kepler_period`, `wavelength`, `ohm_law`,
`de_broglie_wavelength`, `de_broglie`, `compton_wavelength`.

---

## String (`string`)

| Name | Signature | Description |
|------|-----------|-------------|
| `string.len` | `(s: str) -> int` | String length |
| `string.upper` | `(s: str) -> str` | Uppercase |
| `string.lower` | `(s: str) -> str` | Lowercase |
| `string.trim` | `(s: str) -> str` | Trim whitespace |
| `string.capital` | `(s: str) -> str` | Capitalize |
| `string.split` | `(s: str, delim: str) -> array` | Split by delimiter |
| `string.join` | `(arr: array, delim: str) -> str` | Join array |
| `string.concat` | `(a: str, b: str) -> str` | Concatenate |
| `string.has` | `(s: str, sub: str) -> bool` | Contains substring |
| `string.includes` | `(s: str, sub: str) -> bool` | Contains substring |
| `string.startsWith` | `(s: str, prefix: str) -> bool` | Starts with prefix (lowercase alias `startswith`) |
| `string.endsWith` | `(s: str, suffix: str) -> bool` | Ends with suffix (lowercase alias `endswith`) |
| `string.indexOf` | `(s: str, sub: str) -> int` | First index (-1 if not found) |
| `string.lastIndexOf` | `(s: str, sub: str) -> int` | Last index (-1 if not found) |
| `string.find` | `(s: str, sub: str) -> int` | First index |
| `string.findLast` | `(s: str, sub: str) -> int` | Last index |
| `string.findLastIndex` | `(s: str, sub: str) -> int` | Last index |
| `string.findMatch` | `(s: str, pattern: str) -> str` | Regex match |
| `string.match` | `(s: str, pattern: str) -> str` | Regex match |
| `string.replace` | `(s: str, old: str, new: str) -> str` | Replace all |
| `string.replaceAll` | `(s: str, old: str, new: str) -> str` | Replace all |
| `string.sub` | `(s: str, start: int, len: int) -> str` | Substring |
| `string.substr` | `(s: str, start: int, len: int) -> str` | Substring |
| `string.slice` | `(s: str, start: int, end: int) -> str` | Slice |
| `string.left` | `(s: str, n: int) -> str` | Left n chars |
| `string.right` | `(s: str, n: int) -> str` | Right n chars |
| `string.repeat` | `(s: str, n: int) -> str` | Repeat n times (alias `rep`) |
| `string.chr` | `(n: int) -> str` | Character from code point |
| `string.cp` | `(s: str, idx: int) -> str` | Code point at byte index |
| `string.codePointAt` | `(s: str, idx: int) -> int` | Code point at index |
| `string.codePointLen` | `(s: str) -> int` | Number of code points |
| `string.fromCodePoint` | `(n: int) -> str` | String from code point |
| `string.cpAtByte` | `(s: str, idx: int) -> int` | Code point at byte index |
| `string.toCodePointArray` | `(s: str) -> array` | Array of code points |
| `string.byteLen` | `(s: str) -> int` | Byte length |
| `string.bytes` | `(s: str) -> array` | Array of byte values |
| `string.padStart` | `(s: str, len: int, pad?: str) -> str` | Left-pad |
| `string.padEnd` | `(s: str, len: int, pad?: str) -> str` | Right-pad |
| `string.isUpper` | `(s: str) -> bool` | All uppercase |
| `string.isLower` | `(s: str) -> bool` | All lowercase |
| `string.isLetter` | `(s: str) -> bool` | All letters |
| `string.isDigit` | `(s: str) -> bool` | All digits |
| `string.isSpace` | `(s: str) -> bool` | All whitespace |
| `string.isAlphaNum` | `(s: str) -> bool` | All alphanumeric |
| `string.chars` | `(s: str) -> array` | Array of chars |
| `string.each` | `(s: str, fn: fn) -> nil` | Iterate chars |
| `string.filter` | `(s: str, fn: fn) -> str` | Keep matching chars |
| `string.count` | `(s: str, sub: str) -> int` | Count occurrences |

---

## Array (`array`)

| Name | Signature | Description |
|------|-----------|-------------|
| `array.len` | `(a: array) -> int` | Array length |
| `array.push` | `(a: array, val: any) -> int` | Append element |
| `array.pop` | `(a: array) -> any` | Remove and return last |
| `array.unshift` | `(a: array, val: any) -> int` | Prepend element |
| `array.shift` | `(a: array) -> any` | Remove and return first |
| `array.insert` | `(a: array, idx: int, val: any) -> nil` | Insert at index |
| `array.remove` | `(a: array, idx: int) -> any` | Remove at index |
| `array.delete` | `(a: array, val: any) -> bool` | Delete by value |
| `array.has` | `(a: array, val: any) -> bool` | Contains element |
| `array.includes` | `(a: array, val: any) -> bool` | Contains element |
| `array.indexOf` | `(a: array, val: any) -> int` | First index (-1 if not found) |
| `array.findIndex` | `(a: array, fn: fn) -> int` | First index matching predicate |
| `array.find` | `(a: array, fn: fn) -> any` | First element matching predicate |
| `array.findLast` | `(a: array, val: any) -> int` | Last index |
| `array.findLastIndex` | `(a: array, fn: fn) -> int` | Last index matching predicate |
| `array.map` | `(a: array, fn: fn) -> array` | Transform each |
| `array.filter` | `(a: array, fn: fn) -> array` | Keep matching |
| `array.where` | `(a: array, fn: fn) -> array` | Keep matching |
| `array.select` | `(a: array, fn: fn) -> array` | Keep matching |
| `array.reduce` | `(a: array, fn: fn, init: any) -> any` | Reduce to single value |
| `array.each` | `(a: array, fn: fn) -> nil` | Iterate with side effects |
| `array.foreach` | `(a: array, fn: fn) -> nil` | Iterate with side effects |
| `array.every` | `(a: array, fn: fn) -> bool` | All match predicate |
| `array.some` | `(a: array, fn: fn) -> bool` | Any matches predicate |
| `array.sort` | `(a: array) -> array` | Sort ascending |
| `array.sorted` | `(a: array) -> array` | Sort ascending (copy) |
| `array.reverse` | `(a: array) -> array` | Reverse in place |
| `array.reversed` | `(a: array) -> array` | Reverse (copy) |
| `array.slice` | `(a: array, start: int, end: int) -> array` | Sub-array |
| `array.concat` | `(a: array, b: array) -> array` | Concatenate |
| `array.extend` | `(a: array, b: array) -> nil` | Append all of b |
| `array.flatten` | `(a: array) -> array` | Flatten nested arrays |
| `array.zip` | `(a: array, b: array) -> array` | Zip two arrays |
| `array.unique` | `(a: array) -> array` | Remove duplicates |
| `array.groupBy` | `(a: array, fn: fn) -> object` | Group by key fn |
| `array.count` | `(a: array, val: any) -> int` | Count occurrences |
| `array.min` | `(a: array) -> any` | Minimum element |
| `array.max` | `(a: array) -> any` | Maximum element |
| `array.sum` | `(a: array) -> num` | Sum of elements |
| `array.avg` | `(a: array) -> num` | Average of elements |
| `array.join` | `(a: array, delim: str) -> str` | Join as string |
| `array.fill` | `(a: array, val: any) -> array` | Fill with value |
| `array.range` | `(n: int) -> array` | 0..n-1 as array |
| `array.clone` | `(a: array) -> array` | Copy |
| `array.empty` | `(a: array) -> bool` | Is empty |
| `array.clear` | `(a: array) -> nil` | Remove all elements |
| `array.toList` | `(a: array) -> list` | Convert to list |
| `array.toSet` | `(a: array) -> set` | Convert to set |

---

## Object (`object`)

| Name | Signature | Description |
|------|-----------|-------------|
| `object.keys` | `(o: object) -> array` | Get all keys |
| `object.values` | `(o: object) -> array` | Get all values |
| `object.entries` | `(o: object) -> array` | Key-value pairs |
| `object.fromEntries` | `(arr: array) -> object` | Build from pairs |
| `object.has` | `(o: object, key: str) -> bool` | Has key |
| `object.get` | `(o: object, key: str) -> any` | Get value by key |
| `object.set` | `(o: object, key: str, val: any) -> object` | Set value |
| `object.delete` | `(o: object, key: str) -> bool` | Delete key |
| `object.assign` | `(a: object, b: object) -> object` | Copy b's keys onto a |
| `object.merge` | `(a: object, b: object) -> object` | Merge two objects |
| `object.defaults` | `(o: object, d: object) -> object` | Fill missing keys from d |
| `object.omit` | `(o: object, keys: array) -> object` | Drop listed keys |
| `object.omitBy` | `(o: object, fn: fn) -> object` | Drop keys matching fn |
| `object.pick` | `(o: object, keys: array) -> object` | Keep listed keys |
| `object.pickBy` | `(o: object, fn: fn) -> object` | Keep keys matching fn |
| `object.rename` | `(o: object, from: str, to: str) -> object` | Rename key |
| `object.invert` | `(o: object) -> object` | Swap keys and values |
| `object.clone` | `(o: object) -> object` | Shallow copy |
| `object.extend` | `(a: object, b: object) -> object` | Extend a with b |
| `object.deepEqual` | `(a: object, b: object) -> bool` | Deep equality |
| `object.isEmpty` | `(o: object) -> bool` | Is empty |
| `object.empty` | `(o: object) -> bool` | Is empty |
| `object.len` | `(o: object) -> int` | Number of keys |
| `object.size` | `(o: object) -> int` | Number of keys |
| `object.count` | `(o: object, val: any) -> int` | Count occurrences |
| `object.includes` | `(o: object, val: any) -> bool` | Contains value |
| `object.find` | `(o: object, fn: fn) -> any` | First value matching fn |
| `object.map` | `(o: object, fn: fn) -> object` | Transform values |
| `object.filter` | `(o: object, fn: fn) -> object` | Filter key-value |
| `object.reduce` | `(o: object, fn: fn, init: any) -> any` | Reduce to single value |
| `object.each` | `(o: object, fn: fn) -> nil` | Iterate key-value |
| `object.foreach` | `(o: object, fn: fn) -> nil` | Iterate key-value |
| `object.freeze` | `(o: object) -> object` | Freeze |
| `object.isFrozen` | `(o: object) -> bool` | Is frozen |
| `object.seal` | `(o: object) -> object` | Seal |
| `object.isSealed` | `(o: object) -> bool` | Is sealed |
| `object.flatten` | `(o: object) -> object` | Flatten nested |
| `object.unflatten` | `(o: object) -> object` | Unflatten dotted keys |
| `object.sortKey` | `(o: object) -> object` | Sort by key |
| `object.sortVal` | `(o: object) -> object` | Sort by value |
| `object.sorted` | `(o: object) -> object` | Sort by key |
| `object.reversed` | `(o: object) -> object` | Reverse key order |

---

## Type (`type`)

Public type inspection is the builtin `type(value)` (see core builtins).
The `type` module itself only registers internal enum helpers:

| Name | Signature | Description |
|------|-----------|-------------|
| `type._isNumber` | `(val: any) -> bool` | Internal: check number |
| `type._isString` | `(val: any) -> bool` | Internal: check string |
| `type._isArray` | `(val: any) -> bool` | Internal: check array |
| `type._isObject` | `(val: any) -> bool` | Internal: check object |
| `type._isNull` | `(val: any) -> bool` | Internal: check nil |
| `type._isBoolean` | `(val: any) -> bool` | Internal: check bool |
| `type._isEnum` | `(val: any) -> bool` | Internal: check enum |
| `type._newEnum` | `(name: str, variant: str, ...) -> enum` | Internal: create enum type |
| `type._getVariant` | `(e: enum) -> str` | Internal: enum variant name |
| `type._getVariantPayload` | `(e: enum) -> any` | Internal: enum variant payload |

Older drafts documented `type.isInt`, `type.isNum`, `type.isStr`,
`type.isBool`, `type.isNil`, `type.isArray`, `type.isObject`, `type.isFn`,
`type.isClass`, `type.implements` — none of those exist. Use `type()`
comparisons instead, e.g. `type(x) == "int"`.

---

## FS (`fs`)

| Name | Signature | Description |
|------|-----------|-------------|
| `fs.read` | `(path: str) -> str` | Read file contents |
| `fs.write` | `(path: str, content: str) -> nil` | Write file |
| `fs.append` | `(path: str, content: str) -> nil` | Append to file |
| `fs.readLines` | `(path: str) -> array` | Read file as lines |
| `fs.exists` | `(path: str) -> bool` | File exists |
| `fs.size` | `(path: str) -> int` | File size in bytes |
| `fs.stat` | `(path: str) -> object` | File stat info |
| `fs.delete` | `(path: str) -> bool` | Delete file |
| `fs.rm` | `(path: str) -> bool` | Remove file |
| `fs.rename` | `(old: str, new: str) -> bool` | Rename file |
| `fs.copy` | `(src: str, dst: str) -> bool` | Copy file |
| `fs.copyDir` | `(src: str, dst: str) -> bool` | Copy directory |
| `fs.move` | `(src: str, dst: str) -> bool` | Move file |
| `fs.mkdir` | `(path: str) -> bool` | Create directory |
| `fs.mkdirAll` | `(path: str) -> bool` | Create directory tree |
| `fs.rmdir` | `(path: str) -> bool` | Remove directory |
| `fs.readDir` | `(path: str) -> array` | List directory |
| `fs.isDir` | `(path: str) -> bool` | Is directory |
| `fs.isFile` | `(path: str) -> bool` | Is regular file |
| `fs.isSymlink` | `(path: str) -> bool` | Is symlink |
| `fs.symlink` | `(target: str, link: str) -> bool` | Create symlink |
| `fs.readlink` | `(path: str) -> str` | Read symlink target |
| `fs.touch` | `(path: str) -> bool` | Touch file |
| `fs.chmod` | `(path: str, mode: int) -> bool` | Change mode |
| `fs.writable` | `(path: str) -> bool` | Is writable |
| `fs.walk` | `(path: str) -> array` | Walk directory tree |
| `fs.traverse` | `(path: str) -> array` | Traverse directory tree |
| `fs.glob` | `(pattern: str) -> array` | Glob match |
| `fs.open` | `(path: str, mode: str) -> handle` | Open file handle |
| `fs.tempFile` | `() -> str` | Temporary file path |
| `fs.atomicWrite` | `(path: str, content: str) -> nil` | Atomic write |
| `fs.watch` | `(path: str, callback: fn) -> int` | Watch for changes |
| `fs.watchTree` | `(path: str, callback: fn) -> int` | Watch tree for changes |
| `fs.lock` | `(path: str) -> bool` | Lock file |
| `fs.tryLock` | `(path: str) -> bool` | Try to lock file |
| `fs.unlock` | `(path: str) -> bool` | Unlock file |
| `fs.isLocked` | `(path: str) -> bool` | Is locked |

---

## Process (`process`)

| Name | Signature | Description |
|------|-----------|-------------|
| `process.spawn` | `(cmd: str, args?: array) -> int` | Spawn process, return PID |
| `process.run` | `(cmd: str, args?: array) -> str` | Run process, return output |
| `process.runDetached` | `(cmd: str, args?: array) -> int` | Run detached |
| `process.wait` | `(pid: int) -> int` | Wait for process |
| `process.kill` | `(pid: int) -> bool` | Kill process |
| `process.exists` | `(pid: int) -> bool` | Process exists |
| `process.find` | `(name: str) -> array` | Find processes by name |
| `process.nice` | `(pid: int, level: int) -> bool` | Set process priority |
| `process.pid` | `() -> int` | Current PID |
| `process.ppid` | `() -> int` | Parent PID |
| `process.exit` | `(code: int) -> nil` | Exit process |

`runDetached` also exists as a bare global alias: `runDetached(cmd)`
≡ `process.runDetached(cmd)`, and takes the same forms (`"cmd"` or
`["cmd", "arg1", ...]`). The bare alias is a delegation to the same
handler, not a second implementation.

---

## Sys (`sys`)

| Name | Signature | Description |
|------|-----------|-------------|
| `sys.platform` | `() -> str` | "linux", "windows", "macos" |
| `sys.arch` | `() -> str` | "x86_64", "aarch64" |
| `sys.hostname` | `() -> str` | Hostname |
| `sys.username` | `() -> str` | Username |
| `sys.uptime` | `() -> num` | Uptime in seconds |
| `sys.version` | `() -> str` | Havel version |
| `sys.home` | `() -> str` | Home directory |
| `sys.cwd` | `() -> str` | Current directory |
| `sys.tmpdir` | `() -> str` | Temp directory |
| `sys.env` | `(name: str) -> str` | Environment variable |
| `sys.envAll` | `() -> object` | All environment variables |
| `sys.argv` | `() -> array` | Command-line arguments |
| `sys.shell` | `(cmd: str) -> str` | Run shell command |
| `sys.pid` | `() -> int` | Current PID |
| `sys.ppid` | `() -> int` | Parent PID |
| `sys.registerExitCleanup` | `(fn: fn) -> nil` | Register cleanup fn |
| `sys.exit` | `(code: int) -> nil` | Exit process |

---

## Time (`time`)

| Name | Signature | Description |
|------|-----------|-------------|
| `time.now` | `() -> num` | Current Unix timestamp in ms |
| `time.epoch` | `() -> int` | Current Unix timestamp in seconds |
| `time.millis` | `() -> int` | Milliseconds |
| `time.date` | `() -> str` | Current date string |
| `time.time` | `() -> str` | Current time string |
| `time.weekday` | `(ts: num) -> int` | Day of week |
| `time.format` | `(ts: num, fmt: str) -> str` | Format timestamp |
| `time.parse` | `(str: str, fmt: str) -> num` | Parse time string |
| `time.sleep` | `(ms: int) -> nil` | Sleep |
| `time.year` | `(ts: num) -> int` | Extract year |
| `time.month` | `(ts: num) -> int` | Extract month (1-12) |
| `time.day` | `(ts: num) -> int` | Extract day (1-31) |
| `time.hour` | `(ts: num) -> int` | Extract hour (0-23) |
| `time.minute` | `(ts: num) -> int` | Extract minute (0-59) |
| `time.second` | `(ts: num) -> int` | Extract second (0-59) |

`time.duration(ms)` was previously documented here but does not exist.

---

## HTTP (`http`)

| Name | Signature | Description |
|------|-----------|-------------|
| `http.get` | `(url: str, opts?: object) -> object` | HTTP GET |
| `http.post` | `(url: str, body: str, opts?: object) -> object` | HTTP POST |
| `http.put` | `(url: str, body: str, opts?: object) -> object` | HTTP PUT |
| `http.del` | `(url: str, opts?: object) -> object` | HTTP DELETE |
| `http.patch` | `(url: str, body: str, opts?: object) -> object` | HTTP PATCH |
| `http.head` | `(url: str, opts?: object) -> object` | HTTP HEAD |
| `http.download` | `(url: str, path: str) -> bool` | Download to file |
| `http.upload` | `(url: str, path: str) -> object` | Upload file |
| `http.urlEncode` | `(s: str) -> str` | URL-encode |
| `http.urlDecode` | `(s: str) -> str` | URL-decode |
| `http.isOnline` | `() -> bool` | Network reachability |

Response object fields: `status`, `ok`, `body`, `error`, `headers`.
`http.delete` was previously documented here — the real name is `http.del`.

---

## Clipboard (`clipboard`)

| Name | Signature | Description |
|------|-----------|-------------|
| `clipboard.get` | `() -> str` | Get clipboard text |
| `clipboard.set` | `(text: str) -> nil` | Set clipboard text |
| `clipboard.getText` | `() -> str` | Get clipboard text |
| `clipboard.setText` | `(text: str) -> nil` | Set clipboard text |
| `clipboard.hasText` | `() -> bool` | Text present |
| `clipboard.hasImage` | `() -> bool` | Image present |
| `clipboard.getImage` | `() -> image` | Get clipboard image |
| `clipboard.setImage` | `(img: image) -> nil` | Set clipboard image |
| `clipboard.getFiles` | `() -> array` | Get clipboard files |
| `clipboard.setFiles` | `(files: array) -> nil` | Set clipboard files |
| `clipboard.hasFiles` | `() -> bool` | Files present |
| `clipboard.detectMethod` | `() -> str` | Detection method |
| `clipboard.getMethod` | `() -> str` | Current method |
| `clipboard.clear` | `() -> nil` | Clear clipboard |

`clipboard.watch` / `unwatch` / `history` / `historyLimit` / `clearHistory`
were previously documented here but do not exist.

---

## Window (`window`)

Window queries take a window id or a spec object. Verified public members:

### Query

| Name | Description |
|------|-------------|
| `window.active` | Active window object |
| `window.activeId` | Active window id |
| `window.isActive` | Is window active |
| `window.list` | All windows |
| `window.all` | All windows |
| `window.count` | Window count |
| `window.any` | Any windows |
| `window.find` | Find windows by spec |
| `window.findAllBySpec` | Find all by spec |
| `window.findByPid` | Find by process id |
| `window.findByClass` | Find by class |
| `window.findByTitle` | Find by title |
| `window.exists` | Window exists |
| `window.filter` | Filter windows |
| `window.each` | Iterate windows |
| `window.map` | Map over windows |
| `window.sort` | Sort windows |
| `window.title` | Window title |
| `window.class` | Window class |
| `window.exe` | Window executable |
| `window.pid` | Window pid |
| `window.id` | Window id |
| `window.pidWindow` | Window of pid |
| `window.getOpacity` | Get opacity |

### State

| Name | Description |
|------|-------------|
| `window.fullscreen` | Fullscreen state |
| `window.borderless` | Borderless state |
| `window.isBorderless` | Is borderless |
| `window.max` | Maximize |
| `window.min` | Minimize |
| `window.unmax` | Unmaximize |
| `window.unmin` | Unminimize |
| `window.toggleMax` | Toggle maximize |
| `window.toggleBorderless` | Toggle borderless |
| `window.isSticky` | Is sticky |
| `window.sticky` | Stickiness state |
| `window.isShaded` | Is shaded |
| `window.shade` | Shade state |
| `window.isSkipPager` | Skips pager |
| `window.skipPager` | Set skip-pager |
| `window.isSkipTaskbar` | Skips taskbar |
| `window.skipTaskbar` | Set skip-taskbar |
| `window.isAlwaysOnTop` | Always on top |
| `window.alwaysOnTop` | Always-on-top state |
| `window.setAlwaysOnTop` | Set always-on-top |
| `window.stickyToDesktop` | Sticky to desktop |
| `window.frameExtents` | Frame extents |
| `window.area` | Work area |
| `window.viewport` | Viewport |
| `window.snap` | Snap window |
| `window.center` | Center window |
| `window.restore` | Restore window |

### Manipulation

| Name | Description |
|------|-------------|
| `window.focus` | Focus window |
| `window.show` | Show window |
| `window.hide` | Hide window |
| `window.close` | Close window |
| `window.terminate` | Force terminate |
| `window.pin` | Pin window |
| `window.unmap` | Unmap window |
| `window.move` | Move window |
| `window.resize` | Resize window |
| `window.moveResize` | Move and resize |
| `window.moveRel` | Move relative |
| `window.pos` | Window position |
| `window.moveMonitor` | Move to monitor |
| `window.moveToMonitor` | Move to monitor |
| `window.moveMonitorNext` | Move to next monitor |
| `window.moveMonitorPrev` | Move to previous monitor |
| `window.moveToDesktop` | Move to desktop |
| `window.switchDesktop` | Switch desktop |
| `window.currentDesktop` | Current desktop |
| `window.desktopCount` | Desktop count |
| `window.desktopName` | Desktop name |
| `window.getDesktop` | Window's desktop |
| `window.getMonitors` | Monitor info |
| `window.getCurrentMonitor` | Current monitor |
| `window.wait` | Wait for window |
| `window.cmd` | Run window command |
| `window.groupNames` | Group names |
| `window.setOpacity` | Set opacity |

`window.raise` / `window.lower` / `window.geometry` / `window.state` /
`window.minimize` / `window.maximize` / `window.kill` / `window.findOne` /
`window.monitors` / `window.monitorOf` / `window.listVisible` were
previously documented here but do not exist as module functions.

---

## Brightness (`brightness`)

| Name | Signature | Description |
|------|-----------|-------------|
| `brightness.get` | `() -> num` | Get brightness (0-1) |
| `brightness.set` | `(value: num) -> nil` | Set brightness (0-1) |
| `brightness.increase` | `(delta: num) -> nil` | Increase brightness |
| `brightness.decrease` | `(delta: num) -> nil` | Decrease brightness |
| `brightness.increaseGamma` | `(delta: num) -> nil` | Increase gamma |
| `brightness.decreaseGamma` | `(delta: num) -> nil` | Decrease gamma |
| `brightness.getGamma` | `() -> num` | Get gamma |
| `brightness.setGamma` | `(value: num) -> nil` | Set gamma |
| `brightness.getGammaR` | `() -> num` | Get red gamma |
| `brightness.getGammaG` | `() -> num` | Get green gamma |
| `brightness.getGammaB` | `() -> num` | Get blue gamma |
| `brightness.setGammaRGB` | `(r: num, g: num, b: num) -> nil` | Set RGB gamma |
| `brightness.getTemperature` | `() -> int` | Get color temperature |
| `brightness.setTemperature` | `(kelvin: int) -> nil` | Set color temperature |
| `brightness.increaseTemperature` | `(delta: int) -> nil` | Increase temperature |
| `brightness.decreaseTemperature` | `(delta: int) -> nil` | Decrease temperature |
| `brightness.getShadowLift` | `() -> num` | Get shadow lift |
| `brightness.setShadowLift` | `(value: num) -> nil` | Set shadow lift |
| `brightness.getMonitors` | `() -> array` | Monitor list |

---

## Hotkey (`hotkey`)

| Name | Signature | Description |
|------|-----------|-------------|
| `hotkey.register` | `(key, action, policy?, alias?) -> bool` | Register hotkey |
| `hotkey.register_conditional` | `(key, action, condition, alias?) -> bool` | Conditional register |
| `hotkey.trigger` | `(key: str) -> nil` | Trigger hotkey by key |
| `hotkey.list` | `() -> array` | List all hotkeys |

Hotkey registration normally uses the `^+F1 => { ... }` literal syntax
(see docs/language/hotkeys.md); the `hotkey.*` functions exist for dynamic
registration inside loops and objects.

---

**Previous:** [Migration Guide](/guides/migration)
**Next:** [FFI Reference →](/reference/ffi)
