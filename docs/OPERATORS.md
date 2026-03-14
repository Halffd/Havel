# Operator Overloading Guide

Havel supports **Wren-style operator overloading** for structs. Operators are defined using the `op` keyword inside struct definitions.

## Design Philosophy

- **Operators attached to types** - Not universal object system
- **Arrays stay special** - Use `len(arr)` not `arr.length`
- **Simple dispatch** - type → operator table → function
- **Readable syntax** - `op +` not `__add__`
- **No metatable chaos** - Operators are part of type definition

## Binary Operators

Define binary operators with `op` followed by the operator symbol:

```havel
struct Vec2 {
  x
  y
  
  op +(other) {
    return Vec2(x + other.x, y + other.y)
  }
  
  op -(other) {
    return Vec2(x - other.x, y - other.y)
  }
  
  op *(scalar) {
    return Vec2(x * scalar, y * scalar)
  }
  
  op /(scalar) {
    return Vec2(x / scalar, y / scalar)
  }
  
  op %(scalar) {
    return Vec2(x % scalar, y % scalar)
  }
}
```

### Comparison Operators

```havel
struct Vec2 {
  x
  y
  
  op ==(other) {
    return x == other.x && y == other.y
  }
  
  op !=(other) {
    return !(x == other.x && y == other.y)
  }
  
  op <(other) {
    return x < other.x || (x == other.x && y < other.y)
  }
  
  op >(other) {
    return other < this
  }
  
  op <=(other) {
    return !(other < this)
  }
  
  op >=(other) {
    return !(this < other)
  }
}
```

## Unary Operators

Define unary operators with `op` and no parameters:

```havel
struct Vec2 {
  x
  y
  
  op_neg() {
    return Vec2(-x, -y)
  }
  
  op_pos() {
    return this
  }
  
  op_not() {
    return x == 0 && y == 0
  }
}

// Usage
let v = Vec2(1, 2)
let neg = -v      // op_neg
let pos = +v      // op_pos
let zero = !v     // op_not
```

## Special Operators

### Index Operator (`op_index`)

Make your struct indexable with `[]`:

```havel
struct Matrix {
  data
  
  op_index(row, col) {
    return data[row][col]
  }
}

let m = Matrix([[1, 2], [3, 4]])
print(m[0, 1])  // 2
```

### Call Operator (`op_call`)

Make your struct callable like a function:

```havel
struct Counter {
  value
  
  op_call(increment) {
    value += increment
    return value
  }
}

let c = Counter(0)
print(c(5))  // 5
print(c(3))  // 8
```

## Default Methods

### `toString()`

All structs automatically have a `toString()` method:

```havel
struct Point {
  x
  y
}

let p = Point(10, 20)
print(p.toString())  // "Point{x=10, y=20}"
```

You can override it:

```havel
struct Point {
  x
  y
  
  toString() {
    return "(" + x + ", " + y + ")"
  }
}

let p = Point(10, 20)
print(p.toString())  // "(10, 20)"
```

## Complete Example

```havel
struct Vec2 {
  x
  y
  
  // Constructor
  init(x, y) {
    this.x = x
    this.y = y
  }
  
  // Binary operators
  op +(other) {
    return Vec2(x + other.x, y + other.y)
  }
  
  op -(other) {
    return Vec2(x - other.x, y - other.y)
  }
  
  op *(scalar) {
    return Vec2(x * scalar, y * scalar)
  }
  
  // Unary operators
  op_neg() {
    return Vec2(-x, -y)
  }
  
  // Comparison
  op ==(other) {
    return x == other.x && y == other.y
  }
  
  // String representation
  toString() {
    return "Vec2(" + x + ", " + y + ")"
  }
}

// Usage
let v1 = Vec2(1, 2)
let v2 = Vec2(3, 4)

let v3 = v1 + v2
print(v3.toString())  // "Vec2(4, 6)"

let v4 = -v1
print(v4.toString())  // "Vec2(-1, -2)"

if v1 == v2 {
  print("Equal")
}
```

## Operator Reference Table

| Operator | Method | Example |
|----------|--------|---------|
| `+` | `op +` | `a + b` |
| `-` | `op -` | `a - b` |
| `*` | `op *` | `a * b` |
| `/` | `op /` | `a / b` |
| `%` | `op %` | `a % b` |
| `==` | `op ==` | `a == b` |
| `!=` | `op !=` | `a != b` |
| `<` | `op <` | `a < b` |
| `>` | `op >` | `a > b` |
| `<=` | `op <=` | `a <= b` |
| `>=` | `op >=` | `a >= b` |
| `-a` | `op_neg` | `-a` |
| `+a` | `op_pos` | `+a` |
| `!a` | `op_not` | `!a` |
| `a[i]` | `op_index` | `a[i]` |
| `a()` | `op_call` | `a()` |

## Implementation Notes

### Dispatch Order

1. Check if left operand is a struct
2. Check if struct has the operator method
3. If yes, call the operator method
4. If no, use built-in primitive behavior

### Method Binding

Operators receive:
- `this` - The left operand (or the operand for unary)
- `other` - The right operand (for binary operators)

### Performance

Operator overloading has minimal overhead:
- Single type check
- Single method lookup
- Direct method call

No metatable chains or complex dispatch logic.
