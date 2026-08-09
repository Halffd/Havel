---
title: "String Module"
description: "String manipulation: length, case, trimming, splitting, searching, replacing, and substring operations."
---

# String Module

```hv
use string
```

**Source**: `src/havel-lang/stdlib/StringModule.cpp` + `src/havel-lang/stdlib/StringPrototype.cpp` (C++ prototype methods) + `modules/std/string.hv` (Havel namespace wrappers)

---

## Prototype Methods (C++ - StringPrototype.cpp)

These work as method calls: `s.method()` or namespace: `string.method(s)`

| Method | Description | Example |
|--------|-------------|---------|
| `len()` | Length in code points | `"hello".len()` → `5` |
| `upper()` | Uppercase | `"hello".upper()` → `"HELLO"` |
| `lower()` | Lowercase | `"HELLO".lower()` → `"hello"` |
| `capital()` | Capitalize first | `"hello".capital()` → `"Hello"` |
| `has(sub)` | Contains substring | `"hello".has("ell")` → `true` |
| `includes(sub)` | Alias for has | `"hello".includes("ell")` → `true` |
| `split(delim)` | Split by delimiter | `"a,b,c".split(",")` → `["a","b","c"]` |
| `trim()` | Trim whitespace | `" hi ".trim()` → `"hi"` |
| `sub(start, len?)` | Substring | `"hello".sub(1, 3)` → `"ell"` |
| `startsWith(prefix)` | Starts with prefix | `"hello".startsWith("he")` → `true` |
| `endsWith(suffix)` | Ends with suffix | `"hello".endsWith("lo")` → `true` |
| `repeat(n)` | Repeat n times | `"ha".repeat(3)` → `"hahaha"` |
| `count(sub)` | Count occurrences | `"aaa".count("a")` → `3` |
| `indexOf(sub)` | First index (-1 if not found) | `"hello".indexOf("l")` → `2` |
| `find(sub, start?)` | Find index with start | `"hello".find("l", 3)` → `3` |
| `lastIndexOf(sub)` | Last index | `"hello".lastIndexOf("l")` → `3` |
| `findLast(sub)` | Find last occurrence | `"hello".findLast("l")` → `3` |
| `findLastIndex(sub)` | Last index | `"hello".findLastIndex("l")` → `3` |
| `replace(old, new)` | Replace all | `"hello".replace("l", "x")` → `"hexxo"` |
| `match(pattern)` | Regex match | `"abc123".match("\\d+")` → `["123"]` |
| `slice(start, end?)` | Slice | `"hello".slice(1, 4)` → `"ell"` |
| `substr(start, len?)` | Substring | `"hello".substr(1, 3)` → `"ell"` |
| `concat(other)` | Concatenate | `"hello".concat(" world")` → `"hello world"` |
| `reversed()` | Reverse | `"hello".reversed()` → `"olleh"` |
| `map(fn)` | Map over chars | `"abc".map(fn(c){ c.upper() })` → `"ABC"` |
| `filter(fn)` | Filter chars | `"a1b2".filter(fn(c){ c.isDigit() })` → `"12"` |
| `each(fn)` | Iterate chars | `"ab".each(fn(c){ print(c) })` |
| `format(args)` | Format string | `"{0} {1}".format(["hello", "world"])` → `"hello world"` |
| `padStart(width, pad?)` | Left pad | `"5".padStart(3, "0")` → `"005"` |
| `padEnd(width, pad?)` | Right pad | `"hi".padEnd(5, ".")` → `"hi..."` |
| `left(n)` | First n chars | `"hello".left(2)` → `"he"` |
| `right(n)` | Last n chars | `"hello".right(2)` → `"lo"` |

---

## Havel Namespace Functions (modules/std/string.hv)

These provide namespace-style access and additional utilities.

### Wrappers for Prototype Methods

```hv
fn len(s) { s.len() }
fn upper(s) { s.upper() }
fn lower(s) { s.lower() }
fn has(s, sub) { s.has(sub) }
fn includes(s, sub) { s.includes(sub) }
fn split(s, delim) { s.split(delim) }
fn trim(s) { s.trim() }
fn capital(s) { s.capital() }
fn startsWith(s, prefix) { s.startsWith(prefix) }
fn endsWith(s, suffix) { s.endsWith(suffix) }
fn rep(s, n) { s.repeat(n) }        // alias (repeat is keyword)
fn count(s, sub) { s.count(sub) }
fn indexOf(s, sub) { s.indexOf(sub) }
fn find(s, sub, start = nil) { ... }
fn lastIndexOf(s, sub) { s.lastIndexOf(sub) }
fn findLast(s, sub) { s.findLast(sub) }
fn findLastIndex(s, sub) { s.findLastIndex(sub) }
fn replace(s, old, new) { s.replace(old, new) }
fn findMatch(s, pattern) { s.match(pattern) }
fn slice(s, start, end = nil) { ... }
fn sub(s, start, end = nil) { ... }
fn concat(s, other) { s.concat(other) }
fn reversed(s) { s.reversed() }
fn map(s, pred) { s.map(pred) }
fn filter(s, pred) { s.filter(pred) }
fn each(s, pred) { s.each(pred) }
fn format(s, args) { s.format(args) }
fn padStart(s, width, pad = " ") { s.padStart(width, pad) }
fn padEnd(s, width, pad = " ") { s.padEnd(width, pad) }
fn left(s, n) { s.left(n) }
fn right(s, n) { s.right(n) }
```

### Character Utilities

```hv
fn chars(s) { ... }   // ["h", "e", "l", "l", "o"]
fn bytes(s) { ... }   // [104, 101, 108, 108, 111]
```

### Character Classification (prototype-dispatchable)

These work as BOTH `s.isUpper()` AND `string.isUpper(s)`:

```hv
fn isUpper(s) { ... }   // all letters uppercase
fn isLower(s) { ... }   // all letters lowercase
fn isDigit(s) { ... }   // all chars are digits
fn isLetter(s) { ... }  // all chars are letters
fn isAlphaNum(s) { ... } // all chars alphanumeric
fn isSpace(s) { ... }   // all chars whitespace
```

---

## C++ StringModule.cpp Functions (Internal/Private)

These are registered with `_` prefix for internal use:

| Function | Description |
|----------|-------------|
| `string._fromCodePoint(cp)` | Code point → string |
| `string.chr(cp)` | Alias for `_fromCodePoint` |
| `string._codePointLen(s)` | Length in code points |
| `string._toCodePointArray(s)` | Array of [cp, bytePos, byteLen] |
| `string._regexReplace(s, pattern, replacement)` | Regex replace |
| `string.join(arr, delim)` | Join array with delimiter |

---

## Example Usage

```hv
use string

// Prototype methods
s = "hello world"
print(s.upper())           // "HELLO WORLD"
print(s.split(" "))        // ["hello", "world"]
print(s.replace("world", "havel"))  // "hello havel"

// Namespace functions
print(string.upper("hello"))      // "HELLO"
print(string.split("a,b,c", ",")) // ["a", "b", "c"]

// Character classification (prototype dispatch)
print("HELLO".isUpper())   // true
print("hello".isLower())   // true
print("123".isDigit())     // true
print("abc".isLetter())    // true
print("abc123".isAlphaNum()) // true
print("   ".isSpace())     // true
```

---

**Previous:** [Math Module](/stdlib/math)
**Next:** [Array Module →](/stdlib/array)