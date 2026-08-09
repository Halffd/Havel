---
title: "Time Module"
description: "Time operations: now, epoch, format, parse, sleep, and date/time components."
---

# Time Module

```hv
use time
```

**Source**: `src/havel-lang/stdlib/TimeModule.cpp` (C++ host functions) + `modules/std/time.hv` (Havel wrappers)

---

## Functions

### Current Time

| Function | Signature | Description |
|----------|-----------|-------------|
| `time.now()` | `() -> int` | Current epoch in milliseconds |
| `time.epoch()` | `() -> int` | Unix epoch in seconds |
| `time.millis()` | `() -> int` | Same as `now()` - milliseconds since epoch |

```hv
print(time.now())      // 1700000000000
print(time.epoch())    // 1700000000
print(time.millis())   // 1700000000000
```

### Formatting & Parsing

| Function | Signature | Description |
|----------|-----------|-------------|
| `time.format(ts, fmt?)` | `(int, str?) -> str` | Format timestamp as string (default: "%Y-%m-%d %H:%M:%S") |
| `time.parse(datestr, fmt?)` | `(str, str?) -> int?` | Parse date string to millisecond timestamp |

```hv
ts = time.now()
print(time.format(ts))                    // "2024-01-15 14:30:45"
print(time.format(ts, "%Y/%m/%d"))        // "2024/01/15"

parsed = time.parse("2024-01-15 14:30:45")  // 1705331445000
print(parsed)
```

### Sleep

```hv
time.sleep(ms: int) -> nil
```

Non-blocking if in a goroutine (parks the fiber), blocking otherwise.

```hv
time.sleep(1000)  // sleep 1 second
```

### Date/Time Components (Current Local Time)

| Function | Returns | Range |
|----------|---------|-------|
| `time.year()` | `int` | e.g., 2024 |
| `time.month()` | `int` | 1-12 |
| `time.day()` | `int` | 1-31 |
| `time.hour()` | `int` | 0-23 |
| `time.minute()` | `int` | 0-59 |
| `time.second()` | `int` | 0-59 |
| `time.weekday()` | `int` | 0=Sunday, 6=Saturday |
| `time.date()` | `str` | "YYYY-MM-DD" |
| `time.time()` | `str` | "HH:MM:SS" |

```hv
print("Year: " + time.year())
print("Month: " + time.month())
print("Day: " + time.day())
print("Hour: " + time.hour())
print("Minute: " + time.minute())
print("Second: " + time.second())
print("Weekday (0=Sun): " + time.weekday())
print("Date: " + time.date())   // "2024-01-15"
print("Time: " + time.time())   // "14:30:45"
```

---

## Example Usage

```hv
use time

// Timestamps
ts = time.now()
print("ms since epoch: " + ts)
print("seconds since epoch: " + time.epoch())

// Formatting
print("Formatted: " + time.format(ts))
print("Custom: " + time.format(ts, "%Y-%m-%d %H:%M"))

// Parsing
birthday = time.parse("2000-01-01 00:00:00")
print("Birthday timestamp: " + birthday)

// Current components
print("Today is " + time.date())
print("Time is " + time.time())

// Sleep (non-blocking in goroutines)
go {
    time.sleep(1000)
    print("1 second later")
}
```

---

**Previous:** [Object Module](/stdlib/object)
**Next:** [Filesystem Module →](/stdlib/fs)
**Next:** [Filesystem Module →](/stdlib/fs)