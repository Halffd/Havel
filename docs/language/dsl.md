---
title: "DSL & Input Commands"
description: "Domain-specific language for input automation inside dsl blocks."
---

# DSL & Input Commands

Inside `dsl { }` blocks (where `inInputContext = true`), a specialized syntax is available for input automation.

---

## DSL Block

```hv
dsl {
    // Input commands here
}
```

The `dsl` keyword creates a context where the following commands are available as statements.

---

## Core Operators

### Sleep / Delay

```hv
: 500       // sleep 500ms
:1s         // sleep 1 second (time literals)
:100ms      // explicit milliseconds
```

### Send Text

```hv
> "text"              // send text string
> "hello {name}"      // interpolation works
```

### Send Keystrokes

```hv
{Enter}             // press Enter
{Tab}               // press Tab
{Escape}            // press Escape
{Backspace}         // press Backspace
{Delete}            // press Delete
{Up} {Down} {Left} {Right}  // arrow keys
{F1} .. {F12}       // function keys
{Home} {End} {PageUp} {PageDown}
```

### Modifiers with Keystrokes

```hv
^{c}        // Ctrl+C
+{tab}      // Shift+Tab
!{f4}       // Alt+F4
#{space}    // Super+Space
```

### Get Input/State

```hv
< mouse    // get mouse state
< keyboard // get keyboard state
```

---

## Mouse Commands

```hv
lmb         // left mouse button click
rmb         // right mouse button click
mmb         // middle mouse button click

lmb_down    // press down
lmb_up      // release
rmb_down
rmb_up

click()           // left click
click("right")    // right click
click("middle")   // middle click
double_click()    // double click
```

---

## Mouse Movement

```hv
w(100, 200)       // move mouse to absolute (x, y)
w(100, 200, 500)  // move to (100, 200) over 500ms (smooth)

wr(10, 20)        // relative move (delta x, delta y)
```

---

## Mouse Scroll

```hv
ws(10, 20)        // scroll at current position (dx, dy)
ws(0, -3)         // scroll up 3 clicks
ws(0, 3)          // scroll down 3 clicks
```

---

## Control Flow

### Repeat N Times

```hv
* 3 {
    > "hello"
    : 100
}
```

### While Loop

```hv
*? x < 10 {
    > "x is {x}"
    x = x + 1
}
```

### For Loop

```hv
*: i in 1..10 {
    > "i = {i}"
}
```

### If Block

```hv
? x > 5 {
    > "x is big"
}
```

### When Block (Reactive)

```hv
?; x > 5 {
    > "x became big"
}
```

---

## Advanced Operators

### Repeat Previous Line

```hv
> "hello"
!!        // repeats: > "hello"
```

### Run in Thread

```hv
& {
    // runs in background thread
    > "background"
}
```

### Pipeline (within DSL)

```hv
data |> process |> output
```

### Shell Command

```hv
$ "ls -la"    // execute shell command
```

### Print

```hv
-> result     // print result
```

### Return/Break

```hv
<- x          // return x / break
```

### Write to Config/File

```hv
>> config.key    // write to config
>> file.txt      // write to file
```

### Read from Config/File

```hv
<< config.key    // read from config
<< file.txt      // read from file
```

---

## Hotkey Event Listeners

```hv
on keydown keylist { body }
on keyup keylist { body }
off keydown keylist
off keyup keylist
```

---

## Complete Example: VS Code Workflow

```hv
// VS Code power-user workflow
dsl {
    // Open command palette
    ^{p}
    : 200
    
    // Open recent file
    > "recent"
    {Down}
    {Down}
    {Enter}
    : 500
    
    // Split editor right
    ^{\}
    : 100
    
    // Open terminal
    ^{`}
    : 300
    
    // Run last command
    > "!!"
    {Enter}
}

// Or: Refactor workflow
dsl {
    // Rename symbol
    ^{r}
    : 300
    > "newName"
    {Enter}
    : 500
    
    // Organize imports
    > "organize imports"
    {Enter}
}

// Or: Debug workflow
dsl {
    // Toggle breakpoint
    ^{b}
    : 100
    
    // Start debugging
    {F5}
    : 1000
    
    // Step over
    {F10}
    : 500
    
    // Step into
    {F11}
}
```

---

## Key Reference

| Command | Description |
|---------|-------------|
| `:N` / `:Nms` / `:Ns` | Sleep/delay |
| `> "text"` | Send text |
| `{Key}` | Send keystroke |
| `^{c}` | Modifier + key |
| `< state` | Get input/state |
| `lmb` / `rmb` / `mmb` | Mouse click |
| `lmb_down` / `lmb_up` | Mouse press/release |
| `click("right")` | Click with button |
| `w(x, y)` | Move to absolute |
| `w(x, y, ms)` | Smooth move |
| `wr(dx, dy)` | Relative move |
| `ws(dx, dy)` | Scroll |
| `* N { }` | Repeat N times |
| `*? cond { }` | While loop |
| `*: i in range { }` | For loop |
| `? cond { }` | If block |
| `?; cond { }` | When block (reactive) |
| `!!` | Repeat previous line |
| `& { }` | Run in thread |
| `\|` | Pipeline |
| `$ "cmd"` | Shell command |
| `-> result` | Print |
| `<- x` | Return/break |
| `>> key` | Write to config/file |
| `<< key` | Read from config/file |
| `on keydown keys { }` | Key down listener |
| `on keyup keys { }` | Key up listener |
| `off keydown keys` | Remove key down listener |
| `off keyup keys` | Remove key up listener |

---

**Previous:** [Hotkeys](/language/hotkeys)
**Next:** [Interoperability (FFI) →](/language/ffi)