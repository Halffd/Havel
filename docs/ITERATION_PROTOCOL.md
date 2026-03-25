# Iteration Protocol Design

## Core Protocol

```
iter(obj) → iterator
iterator.next() → {value, done}
```

## Built-in Iterators

### Array Iterator
```
iter([1, 2, 3]) → ArrayIterator
ArrayIterator.next() → {value: 1, done: false}
ArrayIterator.next() → {value: 2, done: false}
ArrayIterator.next() → {value: 3, done: false}
ArrayIterator.next() → {value: null, done: true}
```

### String Iterator (character by character)
```
iter("hello") → StringIterator
StringIterator.next() → {value: "h", done: false}
StringIterator.next() → {value: "e", done: false}
...
```

### Object Iterator (key by key)
```
iter({a: 1, b: 2}) → ObjectIterator
ObjectIterator.next() → {value: "a", done: false}
ObjectIterator.next() → {value: "b", done: false}
...
```

### File Iterator (line by line)
```
iter(file) → FileIterator
FileIterator.next() → {value: "line1\n", done: false}
...
```

## For-In Desugaring

```havel
for x in obj {
  body
}

// Desugars to:
let __iter = iter(obj)
while (true) {
  let __result = __iter.next()
  if (__result.done) {
    break
  }
  let x = __result.value
  body
}
```

## Implementation Plan

1. **Add ITER_NEXT opcode** - Returns {value, done} object
2. **Add iter() builtin** - Dispatches based on type
3. **Add Iterator object type** - Internal iterator wrapper
4. **Update for-in compilation** - Use protocol instead of array-specific code
5. **Implement iterators for**: Array, String, Object, (future: File, Generator)

## Extension API

Extensions can provide custom iterators:

```c
// Extension provides iter() for custom type
HavelValue* my_type_iter(HavelValue* obj) {
  return create_iterator(my_type_next, obj);
}

// Register with VM
vm->register_iter_handler(MY_TYPE, my_type_iter);
```
