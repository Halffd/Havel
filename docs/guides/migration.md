---
title: "Migrating from Python/JavaScript/Lua"
description: "Migration guide for developers coming from Python, JavaScript, or Lua."
---

# Migrating from Python/JavaScript/Lua

## From Python

### Syntax Differences

| Python | Havel |
|--------|-------|
| `# comment` | `// comment` |
| `def fn():` | `fn fn() { }` |
| `lambda x: x*2` | `x => x * 2` |
| `if x: ...` | `if x { ... }` |
| `for x in y:` | `for x in y { }` |
| `while x:` | `while x { }` |
| `return x` | (implicit) `x` |
| `None` | `nil` |
| `True/False` | `true/false` |
| `and/or/not` | `&&/||/!` |
| `x is None` | `x == nil` |
| `x == y` | `x == y` |
| `x != y` | `x != y` |
| `raise Err()` | `throw Err()` |
| `try: ... except: ...` | `try { ... } catch { ... }` |
| `with open():` | `try { f = fs.open() } finally { f.close() }` |
| `import x` | `use x` |
| `from x import y` | `use { y } from "x"` |

### Key Differences

1. **No colons after control flow**: `if x { }` not `if x:`
2. **Braces required**: No significant indentation
3. **Implicit return**: Last expression is return value
4. **No `self`**: Use `@` for instance reference
5. **No `static`**: Use `@@` for class variables
6. **String interpolation**: `"{var}"` not `f"{var}"` or `"{}".format(var)`
7. **No `+` for string concat**: Use `"{a}{b}"`
8. **No `len()` builtin**: Use `str.len(s)` or `arr.len(a)`
9. **No `print()` builtin**: Use `print()` (host function)
10. **Variables are function-scoped**, not block-scoped

### Data Structures

```python
# Python
list = [1, 2, 3]
dict = {"a": 1}
set = {1, 2, 3}
tuple = (1, 2, 3)
```

```hv
// Havel
list = [1, 2, 3]
dict = { a: 1 }
set = {1, 2, 3}
tuple = (1, 2, 3)
```

### Classes

```python
# Python
class Point:
    def __init__(self, x, y):
        self.x = x
        self.y = y
    
    def distance(self, other):
        return ((self.x - other.x)**2 + (self.y - other.y)**2)**0.5
    
    @classmethod
    def origin(cls):
        return cls(0, 0)
```

```hv
// Havel
class Point {
    @x = 0
    @y = 0
    
    fn init(x, y) {
        @x = x
        @y = y
    }
    
    fn distance(other) {
        ((@x - other.x)**2 + (@y - other.y)**2)**0.5
    }
    
    @@fn origin() => Point(0, 0)
}
```

---

## From JavaScript

### Syntax Differences

| JavaScript | Havel |
|------------|-------|
| `// comment` | `// comment` |
| `function fn() {}` | `fn fn() { }` |
| `const fn = () => {}` | `fn fn() { }` |
| `x => x * 2` | `x => x * 2` |
| `if (x) {}` | `if x { }` |
| `for (x of y) {}` | `for x in y { }` |
| `while (x) {}` | `while x { }` |
| `return x` | (implicit) `x` |
| `null/undefined` | `nil` |
| `true/false` | `true/false` |
| `&&/||/!` | `&&/||/!` |
| `x === y` | `x == y` |
| `x !== y` | `x != y` |
| `throw new Error()` | `throw "error"` |
| `try { } catch (e) { }` | `try { } catch { }` |
| `import x from 'y'` | `use x from "y"` |
| `export default` | (not needed, all exported) |

### Key Differences

1. **No semicolons**: Line endings separate statements
2. **No `const`/`let`**: Use `val`/`let` or just assignment
3. **No `this`**: Use `@`
4. **No `class` `static`**: Use `@@`
5. **String interpolation**: `"{var}"` not `` `${var}` ``
6. **No `+` for strings**: Use `"{a}{b}"`
7. **Arrays are 0-indexed** (same)
8. **Objects use `:` not `=`**: `{ key: value }`
9. **No prototype inheritance**: Use class inheritance `class Child : Parent`
10. **No `async`/`await` keywords**: Use `co fn` and `<-`/`await`

### Async Patterns

```javascript
// JavaScript
async function fetchData() {
    const resp = await fetch(url);
    return resp.json();
}

fetchData().then(data => console.log(data));
```

```hv
// Havel
co fn fetchData() {
    resp <- http.get(url)
    http.json(resp)
}

go {
    data <- fetchData()
    print(data)
}
```

### Modules

```javascript
// JS: export
export function add(a, b) { return a + b }

// Havel: all top-level exported
fn add(a, b) => a + b
```

```javascript
// JS: import
import { add } from './math.js'

// Havel
use { add } from "math"
```

---

## From Lua

### Syntax Differences

| Lua | Havel |
|-----|-------|
| `-- comment` | `// comment` |
| `function fn() end` | `fn fn() { }` |
| `local x = 5` | `val x = 5` or `x = 5` |
| `if x then ... end` | `if x { ... }` |
| `for i=1,10 do ... end` | `for i in 1..=10 { }` |
| `while x do ... end` | `while x { }` |
| `return x` | (implicit) `x` |
| `nil` | `nil` |
| `true/false` | `true/false` |
| `and/or/not` | `&&/||/!` |
| `x == y` | `x == y` |
| `x ~= y` | `x != y` |
| `error("msg")` | `throw "msg"` |
| `pcall(fn)` | `try { fn() } catch { }` |
| `require "mod"` | `use mod` |

### Key Differences

1. **No `end`**: Use braces `{ }`
2. **No `then`/`do`**: Use braces
3. **No `local`**: Use `val` for immutable
4. **1-indexed → 0-indexed**: Arrays start at 0
5. **Tables → Objects/Arrays**: `{a=1}` → `{ a: 1 }`, `{1,2}` → `[1,2]`
6. **No metatables**: Use classes, traits, operator overloading
7. **No coroutines `coroutine.yield`**: Use `co fn` and `yield`
8. **String concat `..`**: Use `"{a}{b}"`

### Tables to Objects/Arrays

```lua
-- Lua
local list = {1, 2, 3}
local dict = {name = "John", age = 30}
local mixed = {1, "two", {3, 4}}
```

```hv
// Havel
list = [1, 2, 3]
dict = { name: "John", age: 30 }
mixed = [1, "two", [3, 4]]
```

### Metatables → Operator Overloading

```lua
-- Lua
local Vec = {x=0, y=0}
Vec.__add = function(a, b) return Vec:new(a.x+b.x, a.y+b.y) end
Vec.__tostring = function(v) return "Vec("..v.x..","..v.y..")" end
```

```hv
// Havel
struct Vec {
    x: num, y: num
    op +(other) => Vec(@x + other.x, @y + other.y)
    op ""() => "Vec({@x}, {@y})"
}
```

---

## Common Patterns

### Configuration

```python
# Python
config = {"debug": True, "port": 8080}
```

```javascript
// JS
const config = { debug: true, port: 8080 };
```

```hv
// Havel
config = { debug: true, port: 8080 }
// or
config {
    debug = true
    port = 8080
}
```

### File Operations

```python
# Python
with open("file.txt") as f:
    content = f.read()
```

```hv
// Havel
content = fs.read("file.txt")
// or with explicit close
try {
    f = fs.open("file.txt")
    content = f.read()
} finally {
    f.close()
}
```

### HTTP Request

```python
# Python
import requests
resp = requests.get(url)
data = resp.json()
```

```javascript
// JS
const resp = await fetch(url);
const data = await resp.json();
```

```hv
// Havel
resp = http.get(url)
data = http.json(resp)
```

---

**Previous:** [Profiling and Debugging](/guides/profiling)
**Next:** [API Reference →](/reference/host-functions)