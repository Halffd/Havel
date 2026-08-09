---
title: "Object Module"
description: "Object operations: keys, values, entries, map, filter, freeze, seal, and utilities."
---

# Object Module

```hv
use object
# or use Object (capital O)
```

**Source**: `src/havel-lang/stdlib/ObjectModule.cpp` (C++ host functions + prototype methods) + `modules/std/object.hv` (Havel utilities)

---

## C++ Host Functions (ObjectModule.cpp)

All functions registered under `Object.` namespace (capital O). Also available as prototype methods.

### Core Operations

| Function | Signature | Description |
|----------|-----------|-------------|
| `Object.keys` | `(obj: object) -> array` | Get all keys |
| `Object.values` | `(obj: object) -> array` | Get all values |
| `Object.entries` | `(obj: object) -> array` | Get `[[key, value], ...]` |
| `Object.has` | `(obj: object, key: str) -> bool` | Check if key exists |
| `Object.find` | `(obj: object, key: str) -> int` | Find key index (or -1) |
| `Object.size` | `(obj: object) -> int` | Number of keys |
| `Object.len` | `(obj: object) -> int` | Alias for size |
| `Object.isEmpty` | `(obj: object) -> bool` | Check if empty |

### Mutation

| Function | Signature | Description |
|----------|-----------|-------------|
| `Object.set` | `(obj: object, key: str, val: any) -> object` | Set key-value |
| `Object.delete` | `(obj: object, key: str) -> bool` | Delete key |

### Freeze/Seal

| Function | Signature | Description |
|----------|-----------|-------------|
| `Object.freeze` | `(obj: object) -> object` | Freeze (no changes) |
| `Object.seal` | `(obj: object) -> object` | Seal (no add/delete) |
| `Object.isFrozen` | `(obj: object) -> bool` | Check if frozen |
| `Object.isSealed` | `(obj: object) -> bool` | Check if sealed |

### Transformation

| Function | Signature | Description |
|----------|-----------|-------------|
| `Object.map` | `(obj: object, fn: fn) -> object` | Transform values |
| `Object.filter` | `(obj: object, fn: fn) -> object` | Filter by predicate |

---

## Prototype Methods (All Objects)

These work on ANY object: `obj.keys()`, `obj.has("key")`, etc.

| Method | Equivalent | Description |
|--------|------------|-------------|
| `obj.keys()` | `Object.keys(obj)` | Get all keys |
| `obj.values()` | `Object.values(obj)` | Get all values |
| `obj.entries()` | `Object.entries(obj)` | Get `[[key, value], ...]` |
| `obj.has(key)` | `Object.has(obj, key)` | Check if key exists |
| `obj.find(key)` | `Object.find(obj, key)` | Find key index |
| `obj.size()` | `Object.size(obj)` | Number of keys |
| `obj.len()` | `Object.len(obj)` | Alias for size |
| `obj.isEmpty()` | `Object.isEmpty(obj)` | Check if empty |
| `obj.map(fn)` | `Object.map(obj, fn)` | Transform values |
| `obj.filter(fn)` | `Object.filter(obj, fn)` | Filter by predicate |
| `obj.set(key, val)` | `Object.set(obj, key, val)` | Set key-value |
| `obj.delete(key)` | `Object.delete(obj, key)` | Delete key |
| `obj.freeze()` | `Object.freeze(obj)` | Freeze object |
| `obj.seal()` | `Object.seal(obj)` | Seal object |
| `obj.isFrozen()` | `Object.isFrozen(obj)` | Check if frozen |
| `obj.isSealed()` | `Object.isSealed(obj)` | Check if sealed |

---

## Havel Utilities (modules/std/object.hv)

Additional pure-Havel functions using namespace dispatch.

```hv
fn fromEntries(pairs) { ... }      // [[k,v], ...] -> {k:v}
fn assign(target, source) { ... }  // Merge into target
fn defaults(target, source) { ... } // Fill missing keys
fn pick(obj, keys) { ... }         // Pick subset of keys
fn omit(obj, keys) { ... }         // Omit keys
fn rename(obj, old, new) { ... }   // Rename key
fn pickBy(obj, pred) { ... }       // Pick by predicate
fn omitBy(obj, pred) { ... }       // Omit by predicate
fn merge(a, b) { ... }             // Deep merge
```

---

## Example Usage

```hv
use Object

o = { name: "havel", version: 1, active: true }

// Namespace
print(Object.keys(o))        // ["name", "version", "active"]
print(Object.values(o))      // ["havel", 1, true]
print(Object.entries(o))     // [["name","havel"], ["version",1], ["active",true]]
print(Object.has(o, "name")) // true
print(Object.size(o))        // 3

// Prototype methods
print(o.keys())              // ["name", "version", "active"]
print(o.has("version"))      // true
print(o.isEmpty())           // false
print(o.size())              // 3

// Mutation
o.set("newKey", "value")
print(o.newKey)              // "value"
o.delete("active")
print(o.has("active"))       // false

// Transformation
doubled = o.map(fn(v){ v * 2 })  // only affects numbers
filtered = o.filter(fn(k,v){ type(v) == "str" })
```

---

**Previous:** [Array Module](/stdlib/array)
**Next:** [Time Module →](/stdlib/time)