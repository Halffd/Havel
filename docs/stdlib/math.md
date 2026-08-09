---
title: "Math Module"
description: "Mathematical constants, trigonometry, logarithms, powers, clamping, and random numbers."
---

# Math Module

```hv
use math
```

**Source**: `src/havel-lang/stdlib/MathModule.cpp` (C++ host bridges) + `modules/lang/math/math.hv` (pure Havel additions)

---

## Constants

| Constant | Value | Description |
|----------|-------|-------------|
| `math.PI` | 3.14159... | π |
| `math.E` | 2.71828... | Euler's number |
| `math.TAU` | 6.28318... | 2π |
| `math.SQRT2` | 1.41421... | √2 |
| `math.INF` | ∞ | Positive infinity |
| `math.NAN` | NaN | Not-a-number |

---

## C++ Host Functions (from MathModule.cpp)

### Rounding

```hv
math.ceil(x: num) -> int
math.floor(x: num) -> int
math.round(x: num) -> int
```

```hv
math.ceil(3.2)    // 4
math.floor(3.8)   // 3
math.round(3.5)   // 4
math.round(3.4)   // 3
```

### Trigonometry (radians)

```hv
math.sin(x: num) -> num
math.cos(x: num) -> num
math.tan(x: num) -> num
```

```hv
math.sin(math.PI / 2)    // 1
math.cos(0)              // 1
```

### Square Root & Powers

```hv
math.sqrt(x: num) -> num
math.pow(x: num, y: num) -> num
```

```hv
math.sqrt(16)     // 4
math.pow(2, 3)    // 8
```

### Logarithms & Exponential

```hv
math.log(x: num) -> num       // natural log (ln)
math.exp(x: num) -> num       // e^x
```

```hv
math.log(math.E)    // 1
math.exp(1)         // e
```

### Absolute Value & Min/Max

```hv
math.abs(x: num) -> num
math.min(...args: num) -> num  // variadic
math.max(...args: num) -> num  // variadic
```

```hv
math.abs(-5)       // 5
math.min(5, 10, 2) // 2
math.max(5, 10, 2) // 10
```

### Random

```hv
math.random() -> num              // [0, 1)
```

```hv
math.random()    // 0.234...
```

---

## Pure Havel Functions (from modules/lang/math/math.hv)

These are loaded automatically when `use math` is called.

### Constants

| Constant | Value |
|----------|-------|
| `TAU` | 6.28318... (2π) |
| `SQRT2` | 1.41421... |

### Trigonometry

```hv
fn asin(x) { ... }
fn acos(x) { ... }
fn atan2(y, x) { ... }
fn deg2rad(d) { ... }
fn rad2deg(r) { ... }
```

### Clamping & Interpolation

```hv
fn clamp(v, lo, hi) { ... }
fn lerp(a, b, t) { ... }
```

### Rounding

```hv
fn sign(x) { ... }
fn fract(x) { ... }
```

### Logarithms

```hv
fn log2(x) { ... }
fn log10(x) { ... }
fn cbrt(x) { ... }
```

### Distance

```hv
fn distance(x1, y1, x2, y2) { ... }
fn hypot(a, b) { ... }
```

### Modulo (floored)

```hv
fn mod(a, b) { ... }
fn rem(a, b) { ... }
```

### Special Values

```hv
fn is_nan(x) { x != x }
fn is_inf(x) { x == x * 2 && x != 0 }
fn is_finite(x) { !is_nan(x) && !is_inf(x) }
```

### Random

```hv
fn randint(lo, hi) { ... }      // [lo, hi] inclusive
fn random_range(lo, hi) { ... } // [lo, hi)
fn choice(arr) { ... }
```

### Statistics

```hv
fn sum(arr) { ... }
fn mean(arr) { ... }
```

### Miscellaneous

```hv
fn copysign(mag, sgn) { ... }
fn fma(a, b, c) { ... }       // fused multiply-add
```

---

## Example Usage

```hv
use math

// C++ host functions
print("PI = " + math.PI)
print("sin(π/2) = " + math.sin(math.PI / 2))
print("random = " + math.random())

// Havel sidecar functions
print("TAU = " + TAU)
print("clamp(15, 0, 10) = " + clamp(15, 0, 10))
print("randint(1, 10) = " + randint(1, 10))
print("choice([a, b, c]) = " + choice(["a", "b", "c"]))
```

---

**Previous:** [Interoperability (FFI)](/language/ffi)
**Next:** [String Module →](/stdlib/string)