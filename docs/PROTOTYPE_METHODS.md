# Prototype Methods Reference

All built-in methods available on primitive types.

## String Methods

| Method | Returns | Description |
|--------|---------|-------------|
| `len()` | `int` | String length |
| `upper()` | `string` | Uppercase |
| `lower()` | `string` | Lowercase |
| `has(sub)` | `bool` | Contains substring |
| `split(delim)` | `list` | Split by delimiter |
| `trim()` | `string` | Trim whitespace |
| `sub(start, len)` | `string` | Substring |

## List Methods

| Method | Returns | Description |
|--------|---------|-------------|
| `len()` | `int` | List length |
| `push(val)` | `int` | Append to end |
| `pop()` | `value` | Remove from end |
| `has(val)` | `bool` | Contains value (deep equality) |
| `map(fn)` | `list` | Transform each element |
| `filter(fn)` | `list` | Filter by predicate |
| `reduce(fn, init)` | `value` | Aggregate to single value |
| `join(delim)` | `string` | Join with delimiter |

## Integer Methods

| Method | Returns | Description |
|--------|---------|-------------|
| `toHex()` | `string` | Hexadecimal string |
| `toBin()` | `string` | Binary string |

## Float Methods

| Method | Returns | Description |
|--------|---------|-------------|
| `round()` | `int` | Round to nearest integer |
| `floor()` | `int` | Floor to integer |
| `ceil()` | `int` | Ceiling to integer |

## Bool Methods

| Method | Returns | Description |
|--------|---------|-------------|
| `and(b)` | `bool` | Logical AND |
| `or(b)` | `bool` | Logical OR |
| `not()` | `bool` | Logical NOT |
