# Havel Language Grammar Reference

Complete formal grammar and language specification.

---

## Table of Contents

- [Lexical Structure](#lexical-structure)
- [Formal Grammar (EBNF)](#formal-grammar-ebnf)
- [Types System](#type-system)
- [Operator Precedence](#operator-precedence)
- [Keywords](#keywords)
- [Built-in Functions](#built-in-functions)
- [Standard Library](#standard-library)
- [AST Node Types](#ast-node-types)

---

## Lexical Structure

### Tokens

```
token       → identifier | keyword | literal | operator | punctuation
identifier  → letter (letter | digit | '_')*
letter      → [a-zA-Z]
digit       → [0-9]
```

### Comments

```havel
// Single line comment

/* Block comment
   spans multiple lines */
```

### Literals

```havel
// Boolean
true
false

// Null
null

// Numbers
42              // Integer
3.14            // Float
1e10            // Scientific notation
0xFF            // Hexadecimal
0b1010          // Binary

// Strings
"hello"
"hello\nworld"  // Escape sequences
"quote: \"hi\"" // Escaped quotes

// Interpolated strings
let name = "World"
"Hello, ${name}!"    // Expression interpolation
"Value: $name"       // Variable interpolation
```

### Operators

```
// Arithmetic
+ - * / %

// Comparison
== != < > <= >=

// Logical
&& || !

// Assignment
= += -= *= /=

// Increment/Decrement
++ --

// Member access
. []

// Arrow (hotkeys)
=>

// Spread
...

// Range
..
```

---

## Formal Grammar (EBNF)

### Program Structure

```ebnf
program         = { statement } ;
statement       = block
                | declaration
                | expressionStmt
                | controlStmt
                | hotkeyStmt
                | configSection
                | specialStmt
                ;
block           = "{" { statement } "}" ;
```

### Declarations

```ebnf
declaration     = letDecl | constDecl | fnDecl 
                | structDecl | enumDecl | traitDecl | implDecl ;

letDecl         = "let" pattern [ "=" expression ] ";" ;
constDecl       = "const" pattern "=" expression ";" ;

fnDecl          = "fn" identifier "(" [ params ] ")" block ;
params          = param { "," param } ;
param           = identifier [ "=" expression ] ;

structDecl      = "struct" identifier "{" { structMember } "}" ;
structMember    = identifier [ ":" type ] ";" 
                | fnDecl ;

enumDecl        = "enum" identifier "{" { enumVariant } "}" ;
enumVariant     = identifier [ "(" type ")" ] ";" ;

traitDecl       = "trait" identifier "{" { traitMethod } "}" ;
traitMethod     = "fn" identifier "(" [ params ] ")" [ block ] ";" ;

implDecl        = "impl" identifier "for" identifier "{" { fnDecl } "}" ;
```

### Patterns

```ebnf
pattern         = identifier 
                | arrayPattern 
                | objectPattern ;

arrayPattern    = "[" [ pattern { "," pattern } ] "]" ;
objectPattern   = "{" [ identifier ":" pattern { "," identifier ":" pattern } ] "}" ;
```

### Control Flow

```ebnf
controlStmt     = ifStmt | whileStmt | forStmt | loopStmt 
                | repeatStmt | breakStmt | continueStmt | returnStmt ;

ifStmt          = "if" expression block [ "else" ( block | ifStmt ) ] ;
whileStmt       = "while" expression block ;
forStmt         = "for" identifier "in" expression block ;
loopStmt        = "loop" [ "while" expression ] block ;
repeatStmt      = "repeat" expression ( block | statement ) ;
breakStmt       = "break" ";" ;
continueStmt    = "continue" ";" ;
returnStmt      = "return" [ expression ] ";" ;
```

### Hotkey Statements

```ebnf
hotkeyStmt      = hotkey "=>" action
                | hotkey condition "=>" action
                | condition "{" { hotkeyStmt } "}" ;

hotkey          = modifierHotkey | plainHotkey ;
modifierHotkey  = modifier "+" key ;
modifier        = "^" | "!" | "#" | "@" | "~" | "$" ;
plainHotkey     = "F" digit { digit }
                | identifier ;

condition       = "when" expression
                | "if" expression ;

action          = block | statement ;
```

### Config Sections

```ebnf
configSection   = identifier [ args ] "{" { keyValue } "}" ;
args            = literal { literal } ;
keyValue        = ( identifier | keyword ) ( "=" | ":" ) expression ";" ;
```

### Special Statements

```ebnf
specialStmt     = sleepStmt | shellStmt | backtick | inputStmt ;

sleepStmt       = ":" duration ";" ;
duration        = number | string ;

shellStmt       = "$" command ";" ;
backtick        = "`" command "`" ;
command         = identifier { argument } ;

inputStmt       = ">" inputCommand ";" ;
inputCommand    = string | key | mouseAction ;
```

### Expressions

```ebnf
expression      = assignment ;

assignment      = identifier "=" assignment 
                | logicOr ;

logicOr         = logicAnd { "||" logicAnd } ;
logicAnd        = equality { "&&" equality } ;
equality        = comparison { ( "==" | "!=" ) comparison } ;
comparison      = term { ( "<" | ">" | "<=" | ">=" ) term } ;
term            = factor { ( "+" | "-" ) factor } ;
factor          = unary { ( "*" | "/" | "%" ) unary } ;

unary           = ( "!" | "-" ) unary | postfix ;
postfix         = primary { call | memberAccess | indexAccess } ;

call            = "(" [ arguments ] ")" ;
arguments       = expression { "," expression } ;

memberAccess    = "." identifier ;
indexAccess     = "[" expression "]" ;

primary         = literal
                | identifier
                | array
                | object
                | set
                | fnExpr
                | ifExpr
                | blockExpr
                | "(" expression ")" ;

array           = "[" [ expression { "," expression } ] "]" ;
object          = "{" [ objectMember { "," objectMember } ] "}" ;
objectMember    = identifier ":" expression ;
set             = "#{" [ expression { "," expression } ] "}" ;

fnExpr          = "fn" "(" [ params ] ")" ( "=>" expression | block )
                | identifier "=>" expression ;

ifExpr          = "if" expression block "else" block ;
blockExpr       = "{" { statement } expression "}" ;
```

---

## Type System

### Built-in Types

```
Type            Description                   Example
─────────────────────────────────────────────────────────────
null            Null value                    null
bool            Boolean                       true, false
number          64-bit float                  42, 3.14
string          UTF-8 text                    "hello"
array           Dynamic array                 [1, 2, 3]
object          Key-value map                 {key: value}
set             Unique elements               #{1, 2, 3}
function        First-class function          fn(x) => x * 2
struct          User-defined type             struct Point { x, y }
enum            Sum type                      enum Option { Some, None }
```

### Type Annotations (Optional)

```havel
// Typed variables
let x: Num = 10
let name: Str = "Havel"

// Typed functions
fn add(a: Num, b: Num): Num {
    return a + b
}

// Typed structs
struct Vec2 {
    x: Num
    y: Num
}
```

### Type Conversions

```havel
int(x)          // Convert to integer (truncates)
num(x)          // Convert to double
str(x)          // Convert to string
list(...)       // Create list from arguments
tuple(...)      // Create tuple (fixed-size)
set_(...)       // Create set (unique elements)
```

---

## Operator Precedence

Precedence table (highest to lowest):

```
Level  Operator(s)           Associativity  Description
─────────────────────────────────────────────────────────────────
1      () [] .               Left-to-right  Grouping, indexing, member
2      ! - (unary)           Right-to-left  Logical not, negate
3      * / %                 Left-to-right  Multiply, divide, modulo
4      + -                   Left-to-right  Add, subtract
5      < > <= >=             Left-to-right  Comparison
6      == !=                 Left-to-right  Equality
7      &&                    Left-to-right  Logical and
8      ||                    Left-to-right  Logical or
9      = += -= *= /=         Right-to-left  Assignment
```

### Examples

```havel
// Precedence examples
1 + 2 * 3         // 7, not 9
!true && false    // false
a = b = c         // Right-to-left: a = (b = c)
x + y * z         // x + (y * z)
```

---

## Keywords

### Reserved Keywords

```
Control Flow:       if else while for loop repeat break continue return
Declarations:       let const fn struct enum trait impl
Pattern Matching:   match case default
Exception Handling: try catch finally throw
Modules:            import from use with
Hotkeys:            when on off mode config devices modes
Async:              async await
Literals:           true false null
```

### Contextual Keywords

These can be used as identifiers in some contexts:

```
in out get set new this
```

---

## Built-in Functions

### Global Functions

```havel
// Output
print(...args)          // Print to stdout
println(...args)        // Print with newline
log(message)            // Log message

// Type checking
type(value)             // Get type name
implements(obj, trait)  // Check trait implementation
approx(a, b, eps)       // Fuzzy float comparison

// Type conversion
int(x)                  // Convert to integer
num(x)                  // Convert to number
str(x)                  // Convert to string
list(...args)           // Create list
tuple(...args)          // Create tuple
set_(...args)           // Create set

// Utility
len(array|string)       // Get length
range(start, end)       // Create number array
sleep(ms|duration)      // Sleep for duration
sleepUntil(time)        // Sleep until time
exit([code])            // Exit program
```

### Module Functions

See [Standard Library](#standard-library) for complete list.

---

## Standard Library

### Core Modules

| Module | Description | Key Functions |
|--------|-------------|---------------|
| `app` | Application control | `quit()`, `restart()`, `info()` |
| `config` | Configuration | `get()`, `set()`, `reload()` |
| `debug` | Debugging | `lexer()`, `parser()`, `ast()` |
| `process` | Process management | `list()`, `kill()`, `byName()` |
| `launcher` | Process launching | `run()`, `runAsync()`, `runDetached()` |
| `timer` | Timers | `start()`, `stop()`, `once()` |

### IO Modules

| Module | Description | Key Functions |
|--------|-------------|---------------|
| `io` | Input/output | `mouseMove()`, `send()`, `map()` |
| `mouse` | Mouse control | `click()`, `moveTo()`, `getPos()` |
| `keyboard` | Keyboard | `send()`, `press()`, `release()` |

### Media Modules

| Module | Description | Key Functions |
|--------|-------------|---------------|
| `audio` | Audio control | `getVolume()`, `setVolume()`, `toggleMute()` |
| `media` | Media playback | `play()`, `pause()`, `next()` |
| `mpvcontroller` | MPV player | `volumeUp()`, `seekForward()` |
| `brightnessManager` | Brightness | `getBrightness()`, `setBrightness()` |

### Window & Screen

| Module | Description | Key Functions |
|--------|-------------|---------------|
| `window` | Window management | `getTitle()`, `maximize()`, `move()` |
| `screenshot` | Screenshots | `full()`, `region()`, `monitor()` |
| `pixel` | Pixel operations | `get()`, `match()`, `waitFor()` |
| `image` | Image operations | `find()`, `findAll()`, `wait()` |
| `ocr` | OCR | `read()`, `readRegion()` |

### Browser Automation

| Module | Description | Key Functions |
|--------|-------------|---------------|
| `browser` | Browser control | `connect()`, `goto()`, `click()`, `eval()` |

### File & Network

| Module | Description | Key Functions |
|--------|-------------|---------------|
| `filemanager` | File operations | `read()`, `write()`, `copy()`, `move()` |
| `http` | HTTP requests | `get()`, `post()`, `put()`, `delete()` |

### Text & Data

| Module | Description | Key Functions |
|--------|-------------|---------------|
| `text` | Text processing | `upper()`, `lower()`, `trim()`, `split()` |
| `regex` | Regular expressions | `match()`, `replace()`, `split()` |
| `array` | Array operations | `map()`, `filter()`, `reduce()`, `sort()` |
| `math` | Math functions | `abs()`, `sqrt()`, `sin()`, `cos()` |

### GUI & Utilities

| Module | Description | Key Functions |
|--------|-------------|---------------|
| `gui` | GUI dialogs | `menu()`, `notify()`, `confirm()`, `input()` |
| `altTab` | Window switcher | `show()`, `next()`, `previous()` |
| `mapmanager` | Key mappings | `load()`, `save()`, `add()`, `remove()` |
| `clipboard` | Clipboard | `get()`, `set()`, `clear()` |
| `clipboardmanager` | Clipboard history | `copy()`, `paste()`, `history()` |

---

## AST Node Types

### Statement Nodes

```
NodeType
├── Program
├── BlockStatement
├── LetDeclaration
├── ConstDeclaration
├── FunctionDeclaration
├── StructDeclaration
├── EnumDeclaration
├── TraitDeclaration
├── ImplDeclaration
├── IfStatement
├── WhileStatement
├── ForStatement
├── LoopStatement
├── RepeatStatement
├── BreakStatement
├── ContinueStatement
├── ReturnStatement
├── HotkeyBinding
├── ConditionalHotkey
├── ConfigSection
├── SleepStatement
├── ShellCommandStatement
└── ExpressionStatement
```

### Expression Nodes

```
Expression
├── Identifier
├── BinaryExpression
├── UnaryExpression
├── CallExpression
├── MemberExpression
├── IndexExpression
├── AssignmentExpression
├── ArrayExpression
├── ObjectExpression
├── SetExpression
├── LambdaExpression
├── IfExpression
├── BlockExpression
├── SpreadExpression
└── BacktickExpression
```

---

## Error Handling

### Runtime Errors

```havel
// Errors are logged, not thrown
try {
    riskyOperation()
} catch (e) {
    print("Error: " + e)
} finally {
    cleanup()
}

// Assert for debugging
assert(condition, "error message")
```

### Error Containment

All interpreter exceptions are contained to prevent crashes:

```havel
// Conditional hotkeys with errors won't crash
mode == "gaming" => {
    F1 => undefinedFunction()  // Error logged, continues
}
```

---

## Examples

### Complete Program

```havel
// Variable declarations
let greeting = "Hello"
const PI = 3.14159

// Function definition
fn factorial(n) {
    if n <= 1 {
        return 1
    }
    return n * factorial(n - 1)
}

// Struct with methods
struct Point {
    x
    y
    
    fn init(x, y) {
        this.x = x
        this.y = y
    }
    
    fn distance(other) {
        let dx = this.x - other.x
        let dy = this.y - other.y
        return sqrt(dx*dx + dy*dy)
    }
}

// Config section
display {
    brightness = 0.8
    contrast = 0.9
}

// Hotkeys
Super+Return => { run("alacritty") }
Super+Q => { window.close() }

// Main execution
let p1 = Point(10, 20)
let p2 = Point(30, 40)
print("Distance: " + p1.distance(p2))
print("Config: " + config.display.brightness)
```

---

## License

This grammar reference is part of the Havel project, licensed under MIT.
