# Standard Library Reference

Complete reference for all Havel standard library modules. Each module is registered as a host module providing native C++ functions to Havel scripts.

---

## Core Modules

### MathModule

**Source**: `src/havel-lang/stdlib/MathModule.cpp`

| Function | Args | Returns | Description |
|----------|------|---------|-------------|
| `math.abs(x)` | num | num | Absolute value |
| `math.sqrt(x)` | num | num | Square root |
| `math.pow(x, y)` | num, num | num | Power |
| `math.sin(x)` | num | num | Sine (radians) |
| `math.cos(x)` | num | num | Cosine (radians) |
| `math.tan(x)` | num | num | Tangent (radians) |
| `math.asin(x)` | num | num | Arc sine |
| `math.acos(x)` | num | num | Arc cosine |
| `math.atan(x)` | num | num | Arc tangent |
| `math.atan2(y, x)` | num, num | num | Two-argument arc tangent |
| `math.ceil(x)` | num | int | Ceiling |
| `math.floor(x)` | num | int | Floor |
| `math.round(x)` | num | int | Round to nearest |
| `math.log(x)` | num | num | Natural logarithm |
| `math.log10(x)` | num | num | Base-10 logarithm |
| `math.log2(x)` | num | num | Base-2 logarithm |
| `math.exp(x)` | num | num | Exponential (e^x) |
| `math.min(a, b)` | num, num | num | Minimum |
| `math.max(a, b)` | num, num | num | Maximum |
| `math.clamp(x, lo, hi)` | num, num, num | num | Clamp to range |
| `math.pi` | — | num | Pi constant |
| `math.e` | — | num | Euler's number |
| `math.inf` | — | num | Positive infinity |
| `math.random()` | — | num | Random float [0, 1) |
| `math.randomInt(lo, hi)` | int, int | int | Random integer in range |
| `math.sign(x)` | num | int | Sign (-1, 0, 1) |
| `math.degrees(x)` | num | num | Radians to degrees |
| `math.radians(x)` | num | num | Degrees to radians |
| `math.lerp(a, b, t)` | num, num, num | num | Linear interpolation |
| `math.isNaN(x)` | num | bool | Check for NaN |
| `math.isInf(x)` | num | bool | Check for infinity |

### StringModule

**Source**: `src/havel-lang/stdlib/StringModule.cpp`

| Function | Args | Returns | Description |
|----------|------|---------|-------------|
| `str.len(s)` | str | int | String length |
| `str.upper(s)` | str | str | Uppercase |
| `str.lower(s)` | str | str | Lowercase |
| `str.trim(s)` | str | str | Trim whitespace |
| `str.ltrim(s)` | str | str | Trim leading whitespace |
| `str.rtrim(s)` | str | str | Trim trailing whitespace |
| `str.split(s, delim)` | str, str | array | Split by delimiter |
| `str.join(arr, delim)` | array, str | str | Join array with delimiter |
| `str.has(s, sub)` | str, str | bool | Contains substring |
| `str.startsWith(s, prefix)` | str, str | bool | Starts with prefix |
| `str.endsWith(s, suffix)` | str, str | bool | Ends with suffix |
| `str.replace(s, old, new)` | str, str, str | str | Replace all occurrences |
| `str.sub(s, start, len)` | str, int, int | str | Substring |
| `str.reverse(s)` | str | str | Reverse string |
| `str.repeat(s, n)` | str, int | str | Repeat n times |
| `str.char(s, idx)` | str, int | str | Character at index |
| `str.ord(s)` | str | int | Unicode code point of first character |
| `str.chr(n)` | int | str | Character from code point |
| `str.padLeft(s, len, pad)` | str, int, str | str | Left-pad to length |
| `str.padRight(s, len, pad)` | str, int, str | str | Right-pad to length |
| `str.toInt(s)` | str | int | Parse integer |
| `str.toNum(s)` | str | num | Parse float |
| `str.format(fmt, args...)` | str, ... | str | Format string |

### ArrayModule

**Source**: `src/havel-lang/stdlib/ArrayModule.cpp`

| Function | Args | Returns | Description |
|----------|------|---------|-------------|
| `arr.len(a)` | array | int | Array length |
| `arr.push(a, val)` | array, any | nil | Append element |
| `arr.pop(a)` | array | any | Remove and return last |
| `arr.insert(a, idx, val)` | array, int, any | nil | Insert at index |
| `arr.remove(a, idx)` | array, int | any | Remove at index |
| `arr.has(a, val)` | array, any | bool | Contains element |
| `arr.indexOf(a, val)` | array, any | int | First index of value (-1 if not found) |
| `arr.map(a, fn)` | array, fn | array | Transform each element |
| `arr.filter(a, fn)` | array, fn | array | Keep elements matching predicate |
| `arr.reduce(a, fn, init)` | array, fn, any | any | Reduce to single value |
| `arr.forEach(a, fn)` | array, fn | nil | Iterate with side effects |
| `arr.sort(a)` | array | array | Sort ascending |
| `arr.sortBy(a, fn)` | array, fn | array | Sort with comparator |
| `arr.reverse(a)` | array | array | Reverse in place |
| `arr.slice(a, start, end)` | array, int, int | array | Sub-array |
| `arr.concat(a, b)` | array, array | array | Concatenate |
| `arr.flat(a)` | array | array | Flatten one level |
| `arr.unique(a)` | array | array | Remove duplicates |
| `arr.first(a)` | array | any | First element |
| `arr.last(a)` | array | any | Last element |
| `arr.min(a)` | array | any | Minimum element |
| `arr.max(a)` | array | any | Maximum element |
| `arr.sum(a)` | array | num | Sum of elements |
| `arr.join(a, delim)` | array, str | str | Join as string |

### ObjectModule

**Source**: `src/havel-lang/stdlib/ObjectModule.cpp`

| Function | Args | Returns | Description |
|----------|------|---------|-------------|
| `obj.keys(o)` | object | array | Get all keys |
| `obj.values(o)` | object | array | Get all values |
| `obj.has(o, key)` | object, str | bool | Has key |
| `obj.remove(o, key)` | object, str | any | Remove key |
| `obj.merge(a, b)` | object, object | object | Merge two objects |
| `obj.clone(o)` | object | object | Shallow copy |
| `obj.len(o)` | object | int | Number of keys |
| `obj.forEach(o, fn)` | object, fn | nil | Iterate key-value pairs |
| `obj.map(o, fn)` | object, fn | object | Transform values |
| `obj.filter(o, fn)` | object, fn | object | Filter key-value pairs |
| `obj.toJson(o)` | object | str | Serialize to JSON |
| `obj.fromJson(s)` | str | object | Parse from JSON |

---

## Type and Introspection

### TypeModule

**Source**: `src/havel-lang/stdlib/TypeModule.cpp`

| Function | Args | Returns | Description |
|----------|------|---------|-------------|
| `type(val)` | any | str | Type name string |
| `type.isInt(val)` | any | bool | Check if integer |
| `type.isNum(val)` | any | bool | Check if float |
| `type.isStr(val)` | any | bool | Check if string |
| `type.isBool(val)` | any | bool | Check if boolean |
| `type.isNil(val)` | any | bool | Check if nil |
| `type.isArray(val)` | any | bool | Check if array |
| `type.isObject(val)` | any | bool | Check if object |
| `type.isFn(val)` | any | bool | Check if function |
| `type.isClass(val)` | any | bool | Check if class prototype |
| `type.implements(val, trait)` | any, str | bool | Check trait conformance |

---

## I/O and System

### FsModule

**Source**: `src/havel-lang/stdlib/FsModule.cpp`

| Function | Args | Returns | Description |
|----------|------|---------|-------------|
| `fs.read(path)` | str | str | Read file contents |
| `fs.write(path, content)` | str, str | nil | Write file contents |
| `fs.append(path, content)` | str, str | nil | Append to file |
| `fs.exists(path)` | str | bool | File exists |
| `fs.size(path)` | str | int | File size in bytes |
| `fs.delete(path)` | str | bool | Delete file |
| `fs.rename(old, new)` | str, str | bool | Rename file |
| `fs.copy(src, dst)` | str, str | bool | Copy file |
| `fs.move(src, dst)` | str, str | bool | Move file |
| `fs.mkdir(path)` | str | bool | Create directory |
| `fs.readdir(path)` | str | array | List directory contents |
| `fs.isdir(path)` | str | bool | Is directory |
| `fs.isfile(path)` | str | bool | Is regular file |
| `fs.cwd()` | — | str | Current working directory |
| `fs.abspath(path)` | str | str | Absolute path |
| `fs.basename(path)` | str | str | File name component |
| `fs.dirname(path)` | str | str | Directory component |
| `fs.ext(path)` | str | str | File extension |
| `fs.stem(path)` | str | str | File name without extension |

### ShellModule

**Source**: `src/havel-lang/stdlib/ShellModule.cpp`

| Function | Args | Returns | Description |
|----------|------|---------|-------------|
| `shell.run(cmd)` | str | obj | Run command, return `{stdout, stderr, exitCode}` |
| `shell.runDetached(cmd)` | str | int | Run async, return PID |
| `shell.escape(arg)` | str | str | Shell-escape argument |
| `shell.getEnv(key)` | str | str | Get environment variable |
| `shell.setEnv(key, val)` | str, str | nil | Set environment variable |
| `shell.getpid()` | — | int | Current process PID |
| `shell.getppid()` | — | int | Parent process PID |

### SysModule

**Source**: `src/havel-lang/stdlib/SysModule.cpp`

| Function | Args | Returns | Description |
|----------|------|---------|-------------|
| `sys.detect()` | — | obj | System info (OS, arch, kernel) |
| `sys.hardware()` | — | obj | Hardware info (CPU, memory) |
| `sys.time()` | — | num | Unix timestamp (seconds) |
| `sys.timeMs()` | — | int | Unix timestamp (milliseconds) |
| `sys.clock()` | — | num | Monotonic clock (seconds) |
| `sys.sleep(ms)` | int | nil | Sleep current thread |
| `sys.exit(code)` | int | nil | Exit process |

---

## Data and Formatting

### RegexModule

**Source**: `src/havel-lang/stdlib/RegexModule.cpp`

| Function | Args | Returns | Description |
|----------|------|---------|-------------|
| `regex.match(pattern, str)` | str, str | bool | Test if pattern matches |
| `regex.search(pattern, str)` | str, str | array | Find all matches |
| `regex.replace(pattern, str, repl)` | str, str, str | str | Replace matches |
| `regex.split(pattern, str)` | str, str | array | Split by pattern |
| `regex.test(pattern, str)` | str, str | bool | Alias for match |

### FormatModule

**Source**: `src/havel-lang/stdlib/FormatModule.cpp`

| Function | Args | Returns | Description |
|----------|------|---------|-------------|
| `fmt.num(n, precision)` | num, int | str | Format number with decimals |
| `fmt.hex(n)` | int | str | Format as hex |
| `fmt.bin(n)` | int | str | Format as binary |
| `fmt.oct(n)` | int | str | Format as octal |
| `fmt.pad(n, width)` | int, int | str | Pad with zeros |

### TimeModule

**Source**: `src/havel-lang/stdlib/TimeModule.cpp`

| Function | Args | Returns | Description |
|----------|------|---------|-------------|
| `time.now()` | — | num | Current time as Unix timestamp |
| `time.format(ts, fmt)` | num, str | str | Format timestamp |
| `time.parse(str, fmt)` | str, str | num | Parse time string |
| `time.year(ts)` | num | int | Extract year |
| `time.month(ts)` | num | int | Extract month |
| `time.day(ts)` | num | int | Extract day |
| `time.hour(ts)` | num | int | Extract hour |
| `time.minute(ts)` | num | int | Extract minute |
| `time.second(ts)` | num | int | Extract second |

---

## Utilities

### RandomModule

**Source**: `src/havel-lang/stdlib/RandomModule.cpp`

| Function | Args | Returns | Description |
|----------|------|---------|-------------|
| `random.int(lo, hi)` | int, int | int | Random integer in [lo, hi) |
| `random.float(lo, hi)` | num, num | num | Random float in [lo, hi) |
| `random.choice(arr)` | array | any | Random element |
| `random.shuffle(arr)` | array | array | Shuffle in place |
| `random.bool()` | — | bool | Random boolean |
| `random.string(len)` | int | str | Random alphanumeric string |
| `random.seed(n)` | int | nil | Seed the RNG |

### BitModule

**Source**: `src/havel-lang/stdlib/BitModule.cpp`

| Function | Args | Returns | Description |
|----------|------|---------|-------------|
| `bit.and(a, b)` | int, int | int | Bitwise AND |
| `bit.or(a, b)` | int, int | int | Bitwise OR |
| `bit.xor(a, b)` | int, int | int | Bitwise XOR |
| `bit.not(a)` | int | int | Bitwise NOT |
| `bit.shl(a, n)` | int, int | int | Left shift |
| `bit.shr(a, n)` | int, int | int | Right shift |
| `bit.countOnes(a)` | int | int | Population count |
| `bit.countZeros(a)` | int | int | Leading/trailing zeros |

### TimerModule

**Source**: `src/havel-lang/stdlib/TimerModule.cpp`

| Function | Args | Returns | Description |
|----------|------|---------|-------------|
| `timer.after(ms, fn)` | int, fn | int | Fire once after ms, returns timer ID |
| `timer.every(ms, fn)` | int, fn | int | Fire every ms, returns timer ID |
| `timer.cancel(id)` | int | bool | Cancel timer |

### LogModule

**Source**: `src/havel-lang/stdlib/LogModule.cpp`

| Function | Args | Returns | Description |
|----------|------|---------|-------------|
| `log.debug(msg)` | str | nil | Debug-level log |
| `log.info(msg)` | str | nil | Info-level log |
| `log.warn(msg)` | str | nil | Warning-level log |
| `log.error(msg)` | str | nil | Error-level log |
| `log.fatal(msg)` | str | nil | Fatal-level log |
| `log.setLevel(level)` | str | nil | Set log level |

---

## Advanced

### BytecodeBuilderModule

**Source**: `src/havel-lang/stdlib/BytecodeBuilderModule.cpp`

Runtime bytecode construction API for metaprogramming and self-hosted compilation:

| Function | Description |
|----------|-------------|
| `bc.begin(name)` | Start new function |
| `bc.end()` | End current function |
| `bc.emit(op, ...)` | Emit instruction |
| `bc.add_const(val)` | Add to constant pool |
| `bc.add_string(s)` | Add to string pool |
| `bc.patch_jump(ip, target)` | Patch jump target |
| `bc.patch_operand(ip, idx, val)` | Patch specific operand |
| `bc.set_local_count(n)` | Set local variable count |
| `bc.execute(name, args)` | Execute compiled function |
| `bc.execute_persistent(name, args)` | Execute with global persistence |
| `bc.instr_count()` | Current instruction count |
| `bc.get_global(name)` | Lookup global variable |
| `bc.set_global(name, val)` | Set global variable |
| `bc.reset()` | Reset builder state |
| `bc.opcode_id(name)` | Get opcode numeric ID |
| `bc.set_script_dir(path)` | Set script directory for imports |

### OptionModule

**Source**: `src/havel-lang/stdlib/OptionModule.cpp`

Optional value (maybe/nullable) type:

| Function | Args | Returns | Description |
|----------|------|---------|-------------|
| `option.some(val)` | any | obj | Create Some |
| `option.none()` | — | obj | Create None |
| `option.isSome(opt)` | obj | bool | Check Some |
| `option.isNone(opt)` | obj | bool | Check None |
| `option.get(opt)` | obj | any | Unwrap value (throws on None) |
| `option.getOr(opt, def)` | obj, any | any | Unwrap with default |
| `option.map(opt, fn)` | obj, fn | obj | Transform inner value |

### PackModule

**Source**: `src/havel-lang/stdlib/PackModule.cpp`

Binary serialization:

| Function | Args | Returns | Description |
|----------|------|---------|-------------|
| `pack.encode(val)` | any | str | Serialize to binary |
| `pack.decode(str)` | str | any | Deserialize from binary |

### HttpModule

**Source**: `src/havel-lang/stdlib/HttpModule.cpp`

| Function | Args | Returns | Description |
|----------|------|---------|-------------|
| `http.get(url)` | str | obj | HTTP GET |
| `http.post(url, body)` | str, str | obj | HTTP POST |
| `http.put(url, body)` | str, str | obj | HTTP PUT |
| `http.delete(url)` | str | obj | HTTP DELETE |

### PhysicsModule

**Source**: `src/havel-lang/stdlib/PhysicsModule.cpp`

Physics constants and unit conversions.

### PointerModule

**Source**: `src/havel-lang/stdlib/PointerModule.cpp`

Low-level pointer operations for FFI integration.

### BrowserModule

**Source**: `src/havel-lang/stdlib/BrowserModule.cpp`

Browser automation via Chrome DevTools Protocol.

---

## Module Registration

All stdlib modules are registered in `StdLibModules.cpp`:

```cpp
void registerStdLibModules(VMApi& api) {
    registerMathModule(api);
    registerStringModule(api);
    registerArrayModule(api);
    // ...
}
```

With lazy loading (`ModuleDescriptor::loaded = false`), module initialization is deferred until first use.
