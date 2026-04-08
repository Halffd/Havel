# Collections Architecture

## Core Principle: Few Runtime Types, Many Syntaxes

Instead of creating new GC types for every collection variant, Havel **lowers** syntactic types to existing runtime primitives with lightweight flags.

---

## The Three Axes

All collections vary along three dimensions:

| Axis | Options |
|------|---------|
| **Order** | ordered, unordered |
| **Keying** | indexed (0..n), keyed (string→value), none |
| **Mutability** | mutable, immutable (frozen) |

---

## Runtime Types (2 core types)

### 1. Array

Ordered, indexed, mutable.

**Flag:** `frozen` (boolean)

When frozen:
- No `push`, `pop`, `insert`, `delete`
- Indexing still works
- Iteration still works
- Becomes a tuple

### 2. Object

Keyed (string→value), mutable.

**Flags:**
- `sorted` (boolean) — insertion order tracking
- `set_like` (boolean) — all values are `true`

When `sorted = false`:
- Faster (no insertion order tracking)
- Iteration order undefined
- Becomes an "unordered object"

When `set_like = true`:
- All values are `true`
- Membership testing is O(1)
- Becomes a "set"

---

## Syntax → Runtime Mapping

| Syntax | Lowers To | Flags |
|--------|-----------|-------|
| `[1, 2, 3]` | Array | `frozen = false` |
| `(1, 2, 3,)` | Array | `frozen = true` |
| `{a: 1, b: 2}` | Object | `sorted = true` |
| `!{a: 1, b: 2}` | Object | `sorted = false` |
| `#[1, 2, 3]` | Object | `set_like = true` |

---

## Tuple Syntax

```havel
let t = (1, 2, 3,)  // trailing comma = tuple
```

**Disambiguation:**

```havel
(1)     // grouping (just the value 1)
(1,)    // tuple of one element
```

This is non-negotiable — without the trailing comma rule, the parser can't distinguish grouping from single-element tuples.

---

## Shared Protocol (Implemented Once)

All collections inherit these methods via prototype dispatch:

```havel
// Size
coll.len              // number of items

// Iteration (all collections are iterable)
coll.each(fn)         // iterate over items
coll.map(fn)          // transform
coll.filter(fn)       // select
coll.reduce(fn, init) // aggregate

// Query
coll.has(item)        // contains?
coll.find(predicate)  // first matching
coll.indexOf(item)    // position (-1 if not found)

// Conversion
coll.list()           // → mutable array
coll.toSet()          // → set
coll.object()         // → object (if key-value)
coll.tuple()          // → frozen array

// Type conversion functions
list(x)               // → mutable array
set(x)                // → set
object(x)             // → object
tuple(x)              // → frozen array
int(x)                // → integer
num(x)                // → decimal
str(x)                // → string
```

---

## Type-Specific Methods

### Array (mutable, ordered, indexed)

```havel
[1,2,3].push(4)       // add to end
[1,2,3].pop()         // remove from end
[1,2,3].shift()       // remove from front
[1,2,3].unshift(0)    // add to front
[1,2,3].insert(1, 99) // at index
[1,2,3].reverse()     // in-place reverse
[1,2,3].sort()        // in-place sort
[1,2,3].sorted()      // returns new sorted array (not in-place)
```

### Tuple (immutable, ordered, indexed)

```havel
(1,2,3)[0]            // 1
(1,2,3).len           // 3
let [a, b, c] = (1,2,3,)  // destructuring
// No push, pop, shift, unshift, insert, delete
```

### Set (unique values, unordered or ordered)

```havel
#[1,2,3].add(4)       // #[1,2,3,4]
#[1,2,3].has(2)       // true
#[1,2,3].delete(2)    // #[1,3]
#[1,2,3].len          // 3
#[1,2,3].list()       // [1,2,3]
#[1,2,3].union(#[3,4,5])       // #[1,2,3,4,5]
#[1,2,3].intersection(#[2,3,4]) // #[2,3]
#[1,2,3].difference(#[2,3])     // #[1]
#[1,2,3].isSubsetOf(#[1,2,3,4]) // true
```

### Unordered Object (faster, no order guarantee)

```havel
!{a:1, b:2}.has("a")   // true
!{a:1, b:2}.keys()     // ["a", "b"] (order not guaranteed)
!{a:1, b:2}.values()   // [1, 2]
!{a:1, b:2}.entries()  // [["a",1], ["b",2]]
!{a:1, b:2}.get("a")   // 1
!{a:1, b:2}.set("c",3) // chainable
!{a:1, b:2}.delete("a")// remove key
!{a:1, b:2}.merge({c:3}) // combine
```

### String Methods

```havel
"hello".len()           // 5
"hello".upper()         // "HELLO"
"hello".lower()         // "hello"
"hello".has("ell")      // true
"hello".split(",")      // ["hello"]
"hello".trim()          // "hello"
"hello".sub(0, 3)       // "hel"
"hello".startsWith("he") // true
"hello".endsWith("lo")  // true
"hello".repeat(3)       // "hellohellohello"
"hello".indexOf("l")    // 2
"hello".count("l")      // 2
```

### Array Methods (additional)

```havel
[1,2,3].concat([4,5])   // [1,2,3,4,5]
[1,2,3].indexOf(9)      // -1
[1,2,3].find(9)         // null
[1,2,3].every(fn)       // all match
[1,2,3].some(fn)        // any match
[1,2,3].unique()        // [1,2,3] (removes duplicates)
[1,2,3].reversed()      // [3,2,1] (returns new)
```

---

## String and Array Slicing

Planned syntax (Python-style):

```havel
"hello"[:2]     // "he"
"hello"[-2:]    // "lo"
"hello"[-1]     // "o"
"hello"[1:3]    // "el"

[1,2,3][:2]     // [1,2]
[1,2,3][-2:]    // [2,3]
[1,2,3][-1]     // 3
```

---

## What NOT To Do

❌ **Don't create separate GC types** for:
- `SetRef`
- `TupleRef`  
- `UObjectRef`

❌ **Don't duplicate** map/filter/reduce for each type

❌ **Don't add new VM opcodes** unless absolutely necessary

✅ **Do use flags** on existing types

✅ **Do implement shared methods once** on a base protocol

✅ **Do use prototype dispatch** for all collection methods

---

## Future: Protocol-Based Polymorphism

```havel
// Any iterable can be used in for loops
fn process(iterable) {
    for item in iterable {
        print(item)
    }
}

process([1,2,3])     // works - Array is Iterable
process("hello")     // works - String is Iterable
process({a:1,b:2})   // works - Object iterates over values
```

Implemented via:
- `Iterable` protocol: `.next()` or `for...in` support
- `Collection` protocol: `.len()`, `.has()`
- `Indexable` protocol: `coll[index]` access
- `Callable` protocol: `fn(args...)` invocation
