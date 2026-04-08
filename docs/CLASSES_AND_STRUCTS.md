# Classes and Structs

Havel supports two kinds of user-defined types: **structs** (lightweight data containers) and **classes** (full-featured objects with methods, inheritance, and class variables).

---

## Structs

Structs are simple data containers with optional default values. They cannot have methods.

```havel
struct Point {
    x: int = 0,
    y: int = 0
}

let p = Point(10, 20)
print(p.x)  // 10
print(p.y)  // 20

let p2 = Point()  // uses defaults: x=0, y=0
```

### Features

- **Fields with type annotations** (optional): `x: int`
- **Default values**: `x: int = 0`
- **Positional construction**: `Point(10, 20)`
- **No methods**: Structs are pure data
- **Value semantics**: Created fresh each time

### When to Use

- Simple data grouping
- Configuration objects
- Return values with multiple fields
- When you don't need methods or inheritance

---

## Classes

Classes support methods, inheritance, instance variables (`@`), and class variables (`@@`).

```havel
class Counter {
    @@total = 0       // class variable (shared)
    @count = 0        // instance variable (per-object)

    fn init() {       // constructor
        @count = 0
    }

    fn increment() {  // instance method
        @count++
        @@total++
    }

    @@fn getTotal() { // class method
        return @@total
    }
}

let c1 = Counter()
c1.increment()
c1.increment()

let c2 = Counter()
c2.increment()

print(c1.count)         // 2
print(c2.count)         // 1
print(Counter.getTotal()) // 3
```

---

## Instance Variables (`@`)

The `@` prefix accesses instance fields from within methods.

```havel
class Point {
    x: int
    y: int

    fn init(x, y) {
        @x = x    // set instance field
        @y = y
    }

    fn move(dx, dy) {
        @x = @x + dx
        @y = @y + dy
    }

    fn distance(other) {
        let dx = @x - other.x
        let dy = @y - other.y
        math.sqrt(dx*dx + dy*dy)
    }
}
```

### Rules

- `@field` — read instance field
- `@field = value` — write instance field
- `@field++` / `@field--` — compound update
- Inside methods, `@field` is shorthand for accessing the instance's own fields
- Outside methods, use `instance.field` syntax

---

## Class Variables (`@@`)

The `@@` prefix declares or accesses class-level (static) variables shared across all instances.

```havel
class Widget {
    @@count = 0       // shared across all instances

    fn init() {
        @@count++
    }

    @@fn getCount() {
        return @@count
    }
}

let w1 = Widget()
let w2 = Widget()
print(Widget.getCount())  // 2
```

### Rules

- `@@field = value` — declare class variable with default
- `@@field` — read class variable
- `@@field++` — update class variable
- Accessed via `ClassName.field` from outside the class
- Shared across all instances of the class

---

## Class Methods (`@@fn`)

The `@@fn` syntax declares a class method (static method). Class methods have no `self` parameter.

```havel
class Math {
    @@fn add(a, b) {
        return a + b
    }

    @@fn max(a, b) {
        if a > b { a } else { b }
    }
}

print(Math.add(1, 2))   // 3
print(Math.max(5, 3))   // 5
```

### Differences from Instance Methods

| Feature | `fn` (instance) | `@@fn` (class) |
|---------|-----------------|-----------------|
| Self parameter | Yes (slot 0) | No |
| Access to `@field` | Yes | No |
| Access to `@@field` | Yes | Yes |
| Call via | `instance.method()` | `ClassName.method()` |

---

## Instance Methods (`fn`)

Regular methods receive `self` as the first parameter (implicit, at slot 0).

```havel
class Person {
    name: str
    age: int

    fn init(name, age) {
        @name = name
        @age = age
    }

    fn greet(greeting) {
        print(greeting + ", " + @name)
    }

    fn birthday() {
        @age++
    }
}

let p = Person("Alice", 30)
p.greet("Hello")     // Hello, Alice
p.birthday()
print(p.age)         // 31
```

### Syntax Sugar

- `fn methodName()` is shorthand for `@fn methodName()`
- Both declare instance methods
- The `@` is optional before `fn` in class bodies

---

## Inheritance

Use `:` to inherit from a parent class. The child class gets all parent fields and methods.

```havel
class Animal {
    name: str

    fn init(name) {
        @name = name
    }

    fn speak() {
        print(@name + " makes a sound")
    }
}

class Dog : Animal {
    breed: str

    fn init(name, breed) {
        @->init(name)     // call parent init
        @breed = breed
    }

    fn speak() {
        print(@name + " barks!")
    }
}

let d = Dog("Rex", "Labrador")
d.speak()      // Rex barks!
```

### Super Calls (`@->`)

Use `@->method(args)` to call a parent class method.

```havel
class Child : Parent {
    fn init(x, y) {
        @->init(x)    // call Parent.init(x)
        @y = y
    }

    fn overridden() {
        @->overridden()  // call parent's version
        // ... additional behavior
    }
}
```

#### How `@->` Works

1. Loads the parent class prototype from globals
2. Gets the method without binding (via `OBJECT_GET_RAW`)
3. Pushes the current instance (`self`) as the first argument
4. Calls the parent method with the current instance

This ensures the parent method operates on the **current instance**, not the parent's prototype.

### Rules

- Single inheritance only (one parent class)
- Child can override parent methods
- `@->` can call any parent method, not just `init`
- Parent constructor is **not** called automatically — use `@->init()` explicitly

---

## Method Call Dispatch

Havel uses different dispatch strategies depending on the receiver type:

### Primitive Types (No Boxing)

```havel
"hello".len()          // → CALL_METHOD → prototype table lookup
[1, 2, 3].push(4)     // → CALL_METHOD → prototype table lookup
(42).toHex()          // → CALL_METHOD → prototype table lookup
(3.14).round()        // → CALL_METHOD → prototype table lookup
true.not()            // → CALL_METHOD → prototype table lookup
```

- Direct dispatch via prototype tables
- **No boxing** — primitives stay as tagged values
- Fast: hash lookup → function call

### Objects and Classes

```havel
let p = Point(1, 2)
p.distance(other)      // → OBJECT_GET → bound method → CALL
```

1. `OBJECT_GET` retrieves the method and creates a **bound method object** `{fn: ..., self: obj}`
2. The bound method already contains `self`
3. `CALL` invokes with only the explicit arguments

### Super Calls

```havel
@->init(x)             // → LOAD_GLOBAL Parent → OBJECT_GET_RAW → CALL
```

1. Load parent class prototype
2. Get raw method (no binding) via `OBJECT_GET_RAW`
3. Push current instance explicitly
4. Call with self + arguments

---

## Built-in Methods

### String

| Method | Description | Example |
|--------|-------------|---------|
| `len()` | Length | `"hello".len()` → `5` |
| `upper()` | Uppercase | `"hello".upper()` → `"HELLO"` |
| `lower()` | Lowercase | `"HELLO".lower()` → `"hello"` |
| `has(sub)` | Contains | `"hello".has("ell")` → `true` |
| `split(delim)` | Split | `"a,b".split(",")` → `["a", "b"]` |
| `trim()` | Trim whitespace | `" hi ".trim()` → `"hi"` |
| `sub(start, len)` | Substring | `"hello".sub(0, 3)` → `"hel"` |

### Array

| Method | Description | Example |
|--------|-------------|---------|
| `len()` | Length | `[1,2].len()` → `2` |
| `push(val)` | Append | `arr.push(4)` |
| `pop()` | Remove last | `arr.pop()` → last element |
| `has(val)` | Contains (deep) | `arr.has(2)` → `true` |
| `map(fn)` | Transform | `arr.map(fn(x){ x*2 })` |
| `filter(fn)` | Filter | `arr.filter(fn(x){ x > 0 })` |
| `reduce(fn, init)` | Reduce | `arr.reduce(fn(a,b){a+b}, 0)` |
| `join(delim)` | Join | `["a","b"].join("-")` → `"a-b"` |

### Int

| Method | Description | Example |
|--------|-------------|---------|
| `toHex()` | Hex string | `(255).toHex()` → `"ff"` |
| `toBin()` | Binary string | `(5).toBin()` → `"101"` |

### Float

| Method | Description | Example |
|--------|-------------|---------|
| `round()` | Round to int | `(3.7).round()` → `4` |
| `floor()` | Floor | `(3.7).floor()` → `3` |
| `ceil()` | Ceiling | `(3.2).ceil()` → `4` |

### Bool

| Method | Description | Example |
|--------|-------------|---------|
| `and(b)` | Logical AND | `true.and(true)` → `true` |
| `or(b)` | Logical OR | `true.or(false)` → `true` |
| `not()` | Logical NOT | `false.not()` → `true` |

---

## Comparison: Struct vs Class

| Feature | Struct | Class |
|---------|--------|-------|
| Fields | ✅ | ✅ |
| Default values | ✅ | ✅ |
| Methods | ❌ | ✅ |
| `@field` access | N/A | ✅ |
| `@@field` (class vars) | N/A | ✅ |
| Inheritance | ❌ | ✅ |
| `@->` super calls | N/A | ✅ |
| `fn init()` | N/A | ✅ |
| `@@fn` class methods | N/A | ✅ |

---

## Design Decisions

### Why `@` and `@@`?

The sigil-based approach provides:

- **Visual distinction**: `@` = instance, `@@` = class
- **Consistency**: Same syntax for access and declaration
- **No keyword pollution**: No need for `self.`, `this.`, or `static`
- **Familiar to Ruby users**: `@` / `@@` mirrors Ruby's instance/class variables

### Why Not `self.field`?

`self` is a keyword in many languages but adds verbosity. The `@` prefix:
- Is shorter to type
- Is unambiguous in class method context
- Cannot be confused with local variables

### Sealed Primitives

Primitive types (string, int, float, bool, array) have **sealed** method sets. You cannot add methods at runtime:

```havel
// This does NOT work:
string.prototype.foo = fn() { ... }
```

This is intentional for performance — prototype tables are fixed at VM initialization.

### Equality

Currently `==` performs **deep equality** (recursive value comparison). This may change to **reference equality** for objects with a separate `deepEq()` builtin for value comparison.

---

## Runtime Model

### Class Definition

```havel
class Foo {
    @x = 0
    @@count = 0
    fn init() { @x = 0 }
    fn getX() { return @x }
    @@fn getCount() { return @@count }
}
```

Compiles to:
1. `class.define("Foo", [instance_fields], parent_or_null, [class_fields])` — creates prototype object
2. `class.method("Foo", "init", function_obj)` — registers instance method
3. `class.method("Foo", "getX", function_obj)` — registers instance method
4. `class.method("Foo", "getCount", function_obj)` — registers class method
5. Initializes `@@count = 0` on the prototype
6. Stores prototype in global `Foo`

### Instance Creation

```havel
let f = Foo()
```

1. `class.new("Foo")` — creates instance object
2. Binds instance to prototype via `__class` field
3. Initializes instance fields from `__fields` array
4. Calls `init()` method if defined

### Method Dispatch Flow

```
f.getX()
  → compile: CALL_METHOD "getX"
  → VM: lookup "getX" in prototype chain
  → VM: find function object
  → VM: create bound method {fn: func, self: f}
  → VM: CALL with no extra args
  → VM: execute with self at slot 0
```

---

## Common Patterns

### Singleton Pattern

```havel
class Logger {
    @@instance = null

    fn init() {
        // private
    }

    @@fn getInstance() {
        if @@instance == null {
            @@instance = Logger()
        }
        @@instance
    }

    fn log(msg) {
        print(msg)
    }
}

let logger = Logger.getInstance()
logger.log("Hello")
```

### Factory Pattern

```havel
class Shape {
    @@fn createCircle(radius) {
        Circle(radius)
    }

    @@fn createRect(w, h) {
        Rectangle(w, h)
    }
}
```

### Template Method Pattern

```havel
class Game {
    fn play() {
        @initialize()
        @startPlay()
        @endPlay()
    }

    fn initialize() {
        // override in subclass
    }

    fn startPlay() {
        // override in subclass
    }

    fn endPlay() {
        // override in subclass
    }
}

class Chess : Game {
    fn initialize() {
        print("Setting up chess board")
    }

    fn startPlay() {
        print("White moves first")
    }
}
```
