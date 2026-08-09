---
title: "Array Module"
description: "Array operations: map, filter, reduce, sort, slice, and mutation methods."
---

# Array Module

```hv
use array
```

**Source**: `src/havel-lang/stdlib/ArrayModule.cpp` (C++ host functions) + `src/havel-lang/stdlib/ArrayPrototype.cpp` (prototype methods) + `modules/std/array.hv` (Havel utilities)

---

## Prototype Methods (C++ - ArrayPrototype.cpp)

These work as method calls: `arr.method()` or namespace: `array.method(arr)`

| Method | Description | Example |
|--------|-------------|---------|
| `len()` | Length | `[1,2,3].len()` → `3` |
| `push(v)` | Append | `arr.push(4)` |
| `pop()` | Remove last | `[1,2,3].pop()` → `3` |
| `unshift(v)` | Prepend | `arr.unshift(0)` |
| `shift()` | Remove first | `[1,2,3].shift()` → `1` |
| `insert(idx, v)` | Insert at index | `arr.insert(1, 5)` |
| `delete(idx)` | Delete at index | `arr.delete(1)` |
| `clear()` | Clear array | `arr.clear()` |
| `has(v)` | Contains | `[1,2,3].has(2)` → `true` |
| `includes(v)` | Alias for has | `[1,2,3].includes(2)` → `true` |
| `indexOf(v)` | First index | `[1,2,3].indexOf(2)` → `1` |
| `find(pred)` | First matching | `[1,2,3].find(fn(x){ x>1 })` → `2` |
| `map(fn)` | Transform | `[1,2,3].map(fn(x){ x*2 })` → `[2,4,6]` |
| `filter(fn)` | Filter | `[1,2,3,4].filter(fn(x){ x%2==0 })` → `[2,4]` |
| `reduce(fn, init?)` | Reduce | `[1,2,3].reduce(fn(a,b){a+b}, 0)` → `6` |
| `foreach(fn)` | Iterate | `[1,2,3].foreach(fn(x){ print(x) })` |
| `every(fn)` | All match | `[2,4,6].every(fn(x){ x%2==0 })` → `true` |
| `some(fn)` | Any match | `[1,2,3].some(fn(x){ x>2 })` → `true` |
| `join(sep)` | Join to string | `[1,2,3].join("-")` → `"1-2-3"` |
| `concat(other)` | Concatenate | `[1,2].concat([3,4])` → `[1,2,3,4]` |
| `slice(start, end?)` | Slice | `[1,2,3,4].slice(1, 3)` → `[2,3]` |
| `reversed()` | Reverse | `[1,2,3].reversed()` → `[3,2,1]` |
| `unique()` | Remove duplicates | `[1,2,2,3].unique()` → `[1,2,3]` |
| `clone()` | Shallow copy | `arr.clone()` |
| `count(v?)` | Count occurrences / length | `[1,2,2].count(2)` → `2` |
| `sum()` | Sum elements | `[1,2,3].sum()` → `6` |
| `avg()` | Average | `[1,2,3].avg()` → `2` |
| `max()` | Maximum | `[3,1,4].max()` → `4` |
| `min()` | Minimum | `[3,1,4].min()` → `1` |
| `empty()` | Is empty | `[].empty()` → `true` |
| `groupBy(fn)` | Group by key | `[1,2,3,4].groupBy(fn(x){ x%2 })` → `{0:[2,4], 1:[1,3]}` |
| `extend(other)` | Extend in place | `a.extend([4,5])` |
| `reverse()` | Reverse in place | `arr.reverse()` |
| `sort(cmp?)` | Sort in place | `arr.sort()` |
| `delete(idx)` | Delete at index | `arr.delete(1)` |

---

## C++ Host Functions (ArrayModule.cpp)

```hv
array.insert(arr, index, value)  // Insert at index
array.remove(arr, index)         // Remove and return at index
```

These are also registered as prototype methods.

---

## Havel Utilities (modules/std/array.hv)

### Wrappers for Prototype Methods

```hv
fn len(arr) { arr.len() }
fn has(arr, v) { arr.has(v) }
fn indexOf(arr, v) { arr.indexOf(v) }
fn find(arr, pred) { arr.find(pred) }
fn map(arr, pred) { arr.map(pred) }
fn filter(arr, pred) { arr.filter(pred) }
fn reduce(arr, pred, init = nil) { ... }
fn foreach(arr, pred) { arr.foreach(pred) }
fn every(arr, pred) { arr.every(pred) }
fn some(arr, pred) { arr.some(pred) }
fn join(arr, sep) { arr.join(sep) }
fn concat(a, b) { a.concat(b) }
fn slice(arr, start, end = nil) { ... }
fn reversed(arr) { arr.reversed() }
fn unique(arr) { arr.unique() }
fn clone(arr) { arr.clone() }
fn count(arr, v = nil) { ... }
fn includes(arr, v) { arr.includes(v) }
fn sum(arr) { arr.sum() }
fn avg(arr) { arr.avg() }
fn max(arr) { arr.max() }
fn min(arr) { arr.min() }
fn empty(arr) { arr.empty() }
fn groupBy(arr, pred) { arr.groupBy(pred) }
```

### Mutation Wrappers

```hv
fn push(arr, v) { arr.push(v) }
fn pop(arr) { arr.pop() }
fn unshift(arr, v) { arr.unshift(v) }
fn shift(arr) { arr.shift() }
fn insert(arr, idx, v) { arr.insert(idx, v) }
fn reverse(arr) { arr.reverse() }
fn sort(arr, cmp = nil) { ... }
fn extend(a, b) { a.extend(b) }
fn delete(arr, idx) { arr.delete(idx) }
fn clear(arr) { arr.clear() }
```

### Additional Pure-Havel Utilities

```hv
fn flatten(arr) { ... }      // Flatten one level
fn zip(a, b) { ... }         // Zip two arrays
fn range(start, stop?, step?) { ... }  // Generate range
fn fill(len, v) { ... }      // Fill array with value
```

---

## Example Usage

```hv
use array

// Prototype methods
arr = [3, 1, 4, 1, 5]
print(arr.sum())       // 14
print(arr.max())       // 5
print(arr.map(fn(x){ x*2 }))  // [6, 2, 8, 2, 10]

// Namespace functions
print(array.sum([1, 2, 3]))   // 6
print(array.range(0, 10, 2))  // [0, 2, 4, 6, 8]
print(array.flatten([[1,2], [3,4]]))  // [1, 2, 3, 4]
print(array.zip([1,2], [3,4]))       // [[1,3], [2,4]]
print(array.fill(5, 0))              // [0, 0, 0, 0, 0]
```

---

**Previous:** [String Module](/stdlib/string)
**Next:** [Object Module →](/stdlib/object)