# Havel Language Implementation Status

## Overview
This document tracks the implementation status of language features needed to fully support `scripts/example.hv`.

## ✅ Completed Features

### Lexer
- ✅ Basic tokens: identifiers, numbers, strings, keywords
- ✅ Operators: `+`, `-`, `*`, `/`, `%`, `!`
- ✅ Multi-character operators: `&&`, `||`, `==`, `!=`, `<=`, `>=`, `<`, `>`
- ✅ Assignment operator: `=` (distinct from `==`)
- ✅ Delimiters: `(`, `)`, `{`, `}`, `[`, `]`, `,`, `.`, `;`
- ✅ Special tokens: `:` (colon), `?` (question mark), `|` (pipe)
- ✅ Comment support: `#` single-line comments
- ✅ Keywords: `function`, `let`, `if`, `else`, `while`, `for`, `return`, `import`, `from`, `match`, `case`, `default`, `class`, `new`, `this`

### Parser
- ✅ Function declarations
- ✅ Variable declarations (`let`)
- ✅ Basic expressions (binary, unary, literals)
- ✅ Function calls
- ✅ Member access (dot notation)
- ✅ Import statements:
  - ✅ `import * from "module"`
  - ✅ `import a, b, c from "module"`
  - ✅ Import aliasing syntax ready (parser stub exists)

### Interpreter
- ✅ Basic expression evaluation
- ✅ Function execution
- ✅ Variable assignment and lookup
- ✅ Member access on objects
- ✅ Function calls with arguments
- ✅ Import statement execution (basic module loading)

---

## 🚧 In Progress / Partially Complete

### Parser
- 🚧 **Array literals**: `[1, 2, 3]` - Token support exists, parsing not implemented
- 🚧 **Object literals**: `{ key: value }` - Token support exists, parsing not implemented
- 🚧 **Array indexing**: `arr[0]` - Bracket tokens exist, parsing not implemented
- 🚧 **Ternary operator**: `condition ? true_val : false_val` - Tokens exist, parsing not implemented
- 🚧 **Match expressions**: `match` keyword exists but full case/default parsing incomplete
- 🚧 **While loops**: `while` keyword exists but parsing not implemented
- 🚧 **If statements**: `if` keyword exists but parsing not implemented
- 🚧 **Modes**: `modes` using existing HotkeyManager.hpp and ConditionSystem.hpp
- 🚧 **Devices**: `devices` using existing IO.cpp and Device.hpp
- 🚧 **Config**: `config` using existing ConfigManager.hpp

### Interpreter
- 🚧 **Control flow**: if/else execution not implemented
- 🚧 **Loops**: while loop execution not implemented
- 🚧 **Pattern matching**: match/case evaluation not implemented
- 🚧 **Collections**: array/object creation and manipulation not implemented

---

## ❌ Not Yet Started

### Parser - Special Constructs
- ❌ **Config blocks**: `config { ... }` - Need special block parsing
- ❌ **Devices blocks**: `devices { ... }` - Need special block parsing
- ❌ **Modes blocks**: `modes { ... }` - Need special block parsing
- ❌ **Pipeline expressions**: `value | func1() | func2()` - Pipe token exists but parsing not implemented
- ❌ **Implicit function calls**: `command arg1 arg2` (without parentheses)
- ❌ **String interpolation**: Variables inside strings

### Parser - Advanced Features
- ❌ **For loops**: `for item in collection`
- ❌ **Class declarations**: `class Name { ... }`
- ❌ **Object instantiation**: `new ClassName()`
- ❌ **This keyword handling**: `this.property`
- ❌ **Method definitions**: Functions inside classes
- ❌ **Spread operator**: `...array`
- ❌ **Destructuring**: `let { a, b } = obj`

### Interpreter - Core Features
- ❌ **Array operations**: indexing, push, pop, map, filter, etc.
- ❌ **Object operations**: property access, methods
- ❌ **Ternary evaluation**: `?:` operator
- ❌ **Pipeline evaluation**: Chaining function calls
- ❌ **String methods**: substring, split, join, etc.
- ❌ **Type coercion**: Implicit conversions where appropriate

### Interpreter - Advanced Features
- ❌ **Class instantiation**: Object creation with constructors
- ❌ **Method dispatch**: Calling methods on objects
- ❌ **Closure support**: Functions capturing outer scope
- ❌ **Error handling**: Try/catch/finally
- ❌ **Async support**: Promises, async/await

### Standard Library
- ❌ **Built-in functions**: `print()`, `len()`, `range()`, etc.
- ❌ **Math functions**: `Math.floor()`, `Math.random()`, etc.
- ❌ **String functions**: String manipulation utilities
- ❌ **Array utilities**: Array manipulation helpers
- ❌ **I/O functions**: Use fs/FileManager.cpp
- ❌ **System functions**: Env.cpp, ProcessManager.cpp

### Integration
- ❌ **Script file argument**: Modify `main.cpp` to accept script path as first argument
- ❌ **REPL mode**: Interactive interpreter
- ❌ **Module system**: Proper module loading and caching
- ❌ **Standard library path**: Where to find built-in modules

---

## 📝 Example.hv Requirements Analysis

The `scripts/example.hv` file uses these language features:

1. **Imports** (multi-module, wildcard):
   ```hv
   import * from "io"
   import Window, Screen from "x11"
   ```

2. **Config blocks** (object-like with nested structures):
   ```hv
   config {
       defaults: { timeout: 5000, ... }
   }
   ```

3. **Devices blocks** (special declaration syntax):
   ```hv
   devices {
       mouse: "/dev/input/event5"
   }
   ```

4. **Modes blocks** (nested objects with arrays):
   ```hv
   modes {
       class: { Firefox: ["web", "browse"], ... }
   }
   ```

5. **Control flow** (if/while/match):
   ```hv
   if condition { ... }
   while condition { ... }
   match value { case x: ... }
   ```

6. **Collections** (arrays and objects):
   ```hv
   let arr = [1, 2, 3]
   let obj = { key: "value" }
   ```

7. **Pipelines**:
   ```hv
   value | transform() | filter()
   ```

8. **Array indexing and object access**:
   ```hv
   arr[0]
   obj.property
   ```

---

## 🎯 Priority Implementation Order

To get `example.hv` running, implement in this order:

### Phase 1: Collections (Foundation)
1. Array literal parsing and evaluation
2. Object literal parsing and evaluation
3. Array indexing syntax and evaluation
4. Object property access (already works for dot notation)

### Phase 2: Control Flow
1. If/else statement parsing and execution
2. While loop parsing and execution
3. Match/case statement parsing and execution
4. Ternary operator parsing and evaluation

### Phase 3: Special Blocks
1. Config block parsing (treat as object literal), use existing ConfigManager.hpp
2. Devices block parsing (treat as object literal), use existing IO.hpp
3. Modes block parsing (treat as object literal), use existing HotkeyManager.hpp and ConditionSystem.hpp (check how they work first and use them)
4. Store these in interpreter context as special variables

### Phase 4: Advanced Features
0. Ensure modes integrate with condition system, example:
modes {
    gaming:  { class: ["steam", "lutris", "proton", "wine"], title: ["minecraft", "genshin"], ignore: ["chrome", "firefox", "vlc", "obs", "streamlabs"] }
    streaming: { class: ["obs", "streamlabs"], title: ["twitch", "youtube"] }
    coding: { class: ["jetbrains"], title: ["code", "py", "js", "ts"] }
    typing: { class: ["*"], title: ["keybr"] }
    test: { class: ["*"], title: ["*"] }
    verbose: { class: ["*"], title: ["terminal"] }
    default: { class: ["*"], title: ["*"] }
}

on mode gaming {
    print "Switched to gaming mode."
} else {
    print "Switched from gaming to mode " + mode
}
off mode streaming {
    print "Switched from streaming to mode " + mode
}
y when mode gaming => print("You are in gaming mode.")
Enter when mode gaming && title "genshin" => sequence {
    click(); sleep 30
    send "e"; sleep 100
    send "q"; sleep 2000
}
1. Pipeline operator parsing and evaluation. Example: 
^!V => clipboard.get | upper | send
2. Implicit function calls (command-style syntax)
3. String interpolation
4. Make config use ConfigManager.hpp. Example:
config {
    file: "~/Documents/havel.cfg"
    defaults: {
        volume: 50
        brightness: 100
        brightnessStep: 10
    }
}
5. Make devices change device config using the config set before, using ConfigManager.hpp example:
devices {
    keyboard: "INSTANT Keyboard"
    mouse: "USB Mouse"
    joystick: "PS5 Controller"
    mouseSensitivity: 0.2
    ignoreMouse: false
}
keyboard: change Device.Keyboard config
mouse: change Device.Mouse config
joystick: change Device.Joystick config
mouseSensitivity: change Mouse.Sensitivity config
ignoreMouse: change Device.IgnoreMouse config

### Phase 5: Standard Library
1. Built-in debug, IO key code test, input block/unblock and key grab functions
2. Array methods (map, filter, reduce, etc.)
3. String methods
4. I/O, window, brightnessManager functions matching imports in example.hv
Merge all havel-lang tests into a single file to get better build times(command arguments if testing specific things) and make sure they pass

### Phase 6: Integration
1. Script file argument handling in main
2. Module system for imports
3. Error messages and debugging
4. REPL mode

---

## 🔧 Next Immediate Steps

1. **Implement array literal parsing** in `Parser.cpp`
   - Add `parseArrayLiteral()` method
   - Create `ArrayLiteralNode` AST node in `AST.h`

2. **Implement object literal parsing** in `Parser.cpp`
   - Add `parseObjectLiteral()` method
   - Create `ObjectLiteralNode` AST node in `AST.h`

3. **Update interpreter** in `Interpreter.cpp`
   - Add evaluation for `ArrayLiteralNode`
   - Add evaluation for `ObjectLiteralNode`
   - Implement array/object storage in `Value` type

4. **Implement if statement parsing**
   - Add `parseIfStatement()` method
   - Create `IfStatementNode` AST node

5. **Implement if statement execution**
   - Add evaluation for `IfStatementNode` in interpreter

---

## 📊 Estimated Completion

- **Phase 1** (Collections): ~3-4 hours
- **Phase 2** (Control Flow): ~2-3 hours
- **Phase 3** (Special Blocks): ~1-2 hours
- **Phase 4** (Advanced): ~3-4 hours
- **Phase 5** (Stdlib): ~4-5 hours
- **Phase 6** (Integration): ~2-3 hours

**Total estimated time**: 15-21 hours of focused development

---

## 🧪 Testing Strategy

For each feature:
1. Add test in `scripts/test_*.hv`
2. Run via interpreter
3. Verify output/behavior
4. Add unit test in C++ if needed

---

## Notes

- Focus on making `example.hv` parse and execute correctly
- Prioritize features used in `example.hv` over unused features
- Keep AST design extensible for future features
- Consider performance optimizations after correctness is achieved

1️⃣ Add arrays + objects (core syntax foundation)

You cannot interpret config { defaults: { ... } }, modes { ... }, or even map { ... } until these parse.

Implement parseArrayLiteral() and parseObjectLiteral()

Update Value to store std::vector<Value> and std::unordered_map<std::string, Value>

Make array/object printable (for debugging)

✅ Outcome: You can run expressions like:

let x = [1, 2, 3]
let y = { a: 1, b: 2 }
print(x[1] + y.a)


This unlocks half of example.hv already.

2️⃣ Add if statements

You need conditional logic for mode switching, hotkeys, and block behavior.

Implement parseIfStatement() and evaluateIf()

You can do elif/else handling

✅ Outcome: You can execute mode-dependent sections like:

if mode == "gaming" { ... } else { ... }

3️⃣ Implement “block-as-object” for config, devices, modes

Treat these syntactic sugars as special keywords that:

Parse a {} block

Return a map-like Value

Automatically assign it to a global (config, devices, etc.)

You can literally reuse your object literal parser:

if (match(TokenType::ConfigKeyword)) {
    auto obj = parseObjectLiteral();
    interpreter.setGlobal("config", obj);
}


✅ Outcome: Now example.hv starts executing and storing structured configuration.

4️⃣ Add array indexing + assignment

Basic but critical for DSL automation logic:

let x = [1, 2, 3]
x[0] = 10


✅ Outcome: Enables dynamic state (like clipboard history trimming).

5️⃣ Add while + simple loop control

Optional for now. Most of your automation loops can use built-in commands or coroutines later.

⚙️ Implementation Advice

Use ExpressionNode subclasses that return Value::Object or Value::Array.

For object literals, allow both quoted and bare keys (key: and "key":).

Use a helper like:

Value Interpreter::evaluateObject(const ObjectLiteralNode &node) {
    Value::Object obj;
    for (auto &pair : node.pairs)
        obj[pair.first] = evaluate(pair.second);
    return Value(obj);
}


When printing, recursively dump JSON-style — it’s invaluable for debugging.

# Documentation
Update Markdown files, leave only Havel.md for language documentation, README.md for project summary, build and run instructions, TODO.md for missing features and WARP.md for AI usage, celete the other markdown files
# Fix segfaults

>>> fn g(){print("hello world")}
>>> g()
[1]    2293070 segmentation fault (core dumped)  ./build/havel --repl

>>> fn f(){}
>>> f
=> <function>
>>> fn g(num){ return num*num*num}
>>> g(4)



[1]    2283449 segmentation fault (core dumped)  ./build/havel --repl
❯

# Fixn hotkey not working
>>> a => { print("a press") }
Error: Unexpected token in expression: =>
>>> a => print("a")
Error: Unexpected token in expression: =>

1. Arrow Functions Not Parsing ❌
>>> let f = () => { send("a") }
Error: Unexpected token in expression: )

You haven't implemented arrow function parsing yet. Remember that conversation earlier? You need to add the logic to handle () => syntax.

----

2. Object Member Assignment Broken 🐛
>>> o["a"] = 88
=> 88.000000
>>> o
=> {b: bee, a: 0.000000}  // Still 0, not 88!

Your visitAssignmentExpression for index expressions isn't actually mutating the original object. You're getting a copy of the HavelArray/HavelObject, modifying it, but not storing it back.

The Fix:

void Interpreter::visitIndexExpression(const ast::IndexExpression& node) {
    // Current code returns a VALUE
    // But for assignment, you need a REFERENCE
    
    // When used on LEFT side of assignment, you need to:
    // 1. Get the CONTAINER (object/array)
    // 2. Get the INDEX/KEY
    // 3. Store them for later mutation
}

The problem is your interpreter uses value semantics, but assignment needs reference semantics.

Quick Fix - Store References:

// In your HavelValue variant, you need to use shared_ptr for containers
using HavelArray = std::shared_ptr<std::vector<HavelValue>>;
using HavelObject = std::shared_ptr<std::unordered_map<std::string, HavelValue>>;

// Then in visitArrayLiteral:
void Interpreter::visitArrayLiteral(const ast::ArrayLiteral& node) {
    auto array = std::make_shared<std::vector<HavelValue>>();
    
    for (const auto& element : node.elements) {
        auto result = Evaluate(*element);
        if (isError(result)) { lastResult = result; return; }
        array->push_back(unwrap(result));
    }
    
    lastResult = HavelValue(array);
}

// Now assignments will mutate the shared data

----

3. Dot Notation Not Working 🐛
>>> o.b
Error: Member access not implemented for this object type.

Your visitMemberExpression needs to handle objects:

void Interpreter::visitMemberExpression(const ast::MemberExpression& node) {
    auto objectResult = Evaluate(*node.object);
    if (isError(objectResult)) { lastResult = objectResult; return; }
    
    HavelValue objectValue = unwrap(objectResult);
    
    auto* propId = dynamic_cast<const ast::Identifier*>(node.property.get());
    if (!propId) {
        lastResult = HavelRuntimeError("Invalid property access");
        return;
    }
    std::string propName = propId->symbol;
    
    // Handle objects
    if (auto* object = std::get_if<HavelObject>(&objectValue)) {
        auto it = (*object)->find(propName);  // Note the dereference if using shared_ptr
        if (it != (*object)->end()) {
            lastResult = it->second;
            return;
        }
        lastResult = nullptr;  // Property not found
        return;
    }
    
    // Handle arrays (length, etc.)
    if (auto* array = std::get_if<HavelArray>(&objectValue)) {
        if (propName == "length") {
            lastResult = static_cast<double>((*array)->size());
            return;
        }
    }
    
    lastResult = HavelRuntimeError("Member access not supported for this type");
}

----

4. Compound Assignment Missing ❌
>>> a *= 3
Error: Unexpected token in expression: =

You need to add *=, +=, etc. to your lexer and parser. Check if you have these tokens defined:

enum class TokenType {
    // ...
    PlusAssign,     // +=
    MinusAssign,    // -=
    MultiplyAssign, // *=
    DivideAssign,   // /=
    // ...
};

----

5. SEGFAULT on Function Call 💀
>>> fn f(v) { print(v) }
>>> f(9)
Segmentation fault (core dumped)

This is the critical bug. Your function call is crashing
Function declaration pointer is dangling
auto func = std::make_shared<HavelFunction>(HavelFunction{
    &node,  // ⚠️ THIS IS A POINTER TO STACK MEMORY!
    this->environment
});

The &node is a pointer to the AST node, which might be getting destroyed after parsing. You need to either:

	1. Keep the AST alive (store std::unique_ptr<ast::Program> somewhere)
	2. Copy the AST node (not just pointer)

The proper fix:

// Store the entire AST in your interpreter
class Interpreter {
    std::vector<std::unique_ptr<ast::Program>> loadedPrograms;
    
    HavelResult Execute(const std::string& source) {
        auto program = parser.produceAST(source);
        auto* programPtr = program.get();
        loadedPrograms.push_back(std::move(program));  // Keep it alive!
        return Evaluate(*programPtr);
    }
};

Or use std::shared_ptr for AST nodes.

----

6. Range is Exclusive (Not Inclusive) 📏
>>> let a = 4..45
=> [4, 5, ..., 44]  // Doesn't include 45

Your range implementation is exclusive on the end:

for (int i = start; i < end; i += step)  // < not <=

Change to inclusive if you want 4..45 to include 45:

for (int i = start; i <= end; i += step)

----

	1. 🔴 Fix segfault in function calls (AST lifetime issue)
	2. 🟠 Fix object/array mutation (use shared_ptr for containers)
	3. 🟠 Implement dot notation for objects (o.b)
	4. 🟡 Add arrow functions (() => {})
	5. 🟡 Add compound assignment (*=, +=)
	6. 🟢 Add repeat() builtin
	7. 🟢 Decide on inclusive vs exclusive ranges