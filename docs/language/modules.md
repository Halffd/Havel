---
title: "Modules"
description: "Module system: use, import, from, as, all, none, pure imports."
---

# Modules

Havel uses **Python-style modules**: every top-level function, variable, and class is exported. No `export` keyword. Prefix with `_` to indicate "private by convention" (not enforced).

---

## Import / Use

```hv
use module                      // import module by name
use { fn1, fn2 } from "path"   // import specific bindings from path
use a, b from "path"           // import a and b from path
use { a, b as alias } from "path"  // import with aliases
use * from "path"              // wildcard import
use Name as alias              // import with alias
use Name from "path" as alias  // import from path with alias
```

### Examples

```hv
// Import entire module
use math
print(math.pi)
print(math.sin(1.57))

// Import specific functions
use { sin, cos, pi } from "math"
print(sin(pi / 2))

// Import with alias
use { sin as sine } from "math"
print(sine(1.57))

// Wildcard (imports all top-level)
use * from "math"
print(pi)

// Rename module
use math as m
print(m.pi)
```

---

## Module Resolution

### Search Paths

1. Current script directory
2. `HAVEL_MODULE_PATH` environment variable (colon-separated)
3. Built-in stdlib paths
4. `./modules/` relative to executable

### File Structure

```
project/
  main.hv
  utils/
    math.hv
    string.hv
  lib/
    http.hv
```

```hv
// main.hv
use utils.math
use lib.http
```

---

## Module Execution

### `use module`

1. Resolves file via `ModuleLoader::resolve()`
2. Parses and compiles independently
3. Executes in **sandboxed global scope**
4. Returns exports object with all top-level definitions
5. Caller accesses via `module.function()` or destructured imports

### `load("file.hv")`

1. Resolves file path
2. Parses and compiles
3. **Executes in caller's global scope** (not sandboxed)
4. Merges new definitions into caller's globals
5. Returns `true` on success

Key difference: `load()` merges into current scope; `use` returns isolated exports.

---

## Module File Example

```hv
// math.hv
val PI = 3.14159265359

fn sin(x) { /* ... */ }
fn cos(x) { /* ... */ }
fn clamp(x, lo, hi) {
    if x < lo { lo } elif x > hi { hi } else { x }
}

_internal_helper = 42  // private by convention (underscore prefix)
```

```hv
// main.hv
use math
use { clamp } from "math"

print(math.PI)
print(clamp(10, 0, 5))
```

---

## Re-exporting

```hv
// utils.hv
use math
use string

// Re-export all
// (explicit re-export pattern)
fn sin(x) => math.sin(x)
fn cos(x) => math.cos(x)
fn upper(s) => string.upper(s)
```

---

## Circular Imports

Detected via `modules_loading_` set in VM. Throws `E042: circular import` if detected.

---

## Pure Imports

```hv
use pure "module"    // import without executing side effects
```

Only loads type/protocol/trait declarations, skips executable code. Useful for type-only dependencies.

---

## Module Metadata

```hv
// module.hv
config {
    version = "1.0.0"
    description = "Math utilities"
    author = "name"
}
```

Accessible via `module.__config__` (internal).

---

**Previous:** [Concurrency](/language/concurrency)
**Next:** [Hotkeys →](/language/hotkeys)