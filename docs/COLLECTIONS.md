# Collections Roadmap

## Current State

Havel currently supports these collection types:

| Type | Syntax | Ordered | Mutable | Unique Keys |
|------|--------|---------|---------|-------------|
| List | `[1, 2, 3]` | ✅ | ✅ | ❌ (allows duplicates) |
| Object | `{a: 1, b: 2}` | ✅ (insertion) | ✅ | ✅ |

## Planned Types

### Set (`#[1, 2, 3]`)

**Syntax:** `#[1, 2, 3]`

**Properties:**
- Ordered (insertion order)
- Mutable
- Unique values only
- O(1) membership testing

**Methods:**
```havel
let s = #[1, 2, 3]
s.add(4)           // #[1, 2, 3, 4]
s.has(2)           // true
s.delete(2)        // #[1, 3, 4]
s.len              // 3
s.list()           // [1, 3, 4]

#[1,2,3].union(#[3,4,5])         // #[1,2,3,4,5]
#[1,2,3].intersection(#[2,3,4])  // #[2,3]
#[1,2,3].difference(#[2,3])      // #[1]
#[1,2,3].isSubsetOf(#[1,2,3,4]) // true
```

**Implementation Plan:**
1. Add `TokenType::HashOpen` for `#[`
2. Add `ast::SetLiteral` node
3. Add `GCHeap::allocateSet()` and `SetRef`
4. Add `OpCode::SET_NEW`, `SET_ADD`, `SET_HAS`, etc.
5. Register prototype methods

### Unordered Object (`!{a: 1, b: 2}`)

**Syntax:** `!{a: 1, b: 2}`

**Properties:**
- Unordered (hash order)
- Mutable
- Unique keys
- Faster than regular objects (no insertion order tracking)

**Methods:**
```havel
let u = !{a: 1, b: 2, c: 3}
u.has("a")        // true
u.keys()          // ["a", "b", "c"] (order not guaranteed)
u.values()        // [1, 2, 3]
u.entries()       // [["a",1], ["b",2], ["c",3]]
```

**Implementation Plan:**
1. Add `TokenType::BangOpenBrace` for `!{`
2. Parse as `ObjectLiteral` with `unordered = true` flag
3. GCHeap already supports `allocateObject(sorted = true/false)`
4. Skip insertion order tracking for unordered objects

### Tuple (`(1, "two", true)`)

**Syntax:** `(1, "two", true)`

**Properties:**
- Fixed size
- Heterogeneous types
- Immutable
- Supports destructuring

**Methods:**
```havel
let t = (1, "two", true)
t[0]              // 1
t[1]              // "two"
t.len             // 3
let [a, b, c] = t // destructuring
```

**Implementation Plan:**
1. Parse parenthesized expressions with commas as tuples
2. Add `ast::TupleLiteral` node (or reuse existing)
3. Add `GCHeap::allocateTuple()` and `TupleRef`
4. Add indexing support

## Collection Protocol

All collections will support these common methods:

```havel
// Size
coll.len              // number of items
coll.empty?()        // true if len == 0

// Iteration
coll.each(fn)         // iterate over items
coll.map(fn)          // transform
coll.filter(fn)       // select
coll.reduce(fn, init) // aggregate

// Query
coll.has(item)        // contains?
coll.find(predicate)  // first matching
coll.indexOf(item)    // position (-1 if not found)

// Conversion
coll.list()        // → List
coll.toSet()          // → Set
coll.object()       // → Object (if key-value)
coll.tuple()          // → Tuple

// Type conversion functions
list(coll)            // → List
set(coll)             // → Set
object(coll)          // → Object
tuple(coll)           // → Tuple
int(x)                // → integer
num(x)                // → decimal
str(x)                // → string
```

## Duck Typing & Protocols

Future iteration will support protocol-based polymorphism:

```havel
// Any iterable can be used in for loops
fn process(iterable) {
    for item in iterable {
        print(item)
    }
}

process([1,2,3])     // works - List is Iterable
process("hello")     // works - String is Iterable
process({a:1,b:2})   // works - Object iterates over values
```

This would be implemented via:
- `Iterable` protocol: `.next()` method
- `Collection` protocol: `.len()`, `.has()`
- `Indexable` protocol: `coll[index]` access
- `Callable` protocol: `fn(args...)` invocation
