---
title: "DSL & Input Commands"
description: "Domain-specific language for input automation inside dsl blocks and hotkey bodies."
---

# DSL & Input Commands

This page documents the input-automation DSL that is **actually
implemented**. Plenty of older drafts documented syntax that was never
written; the "not implemented" list at the bottom keeps those out of
production scripts.

## Pipelines this applies to

- **Self-hosted** (`havel --run --self-hosted-path out`, `hvtest`): full
  support.
- **C++ bootstrap**: parses `dsl {}`, `> "text"`, `: N` sleep, `? cond {}`,
  `* N {}`, `$ "cmd"`. Bare mouse/key forms below are self-hosted only.

Input commands degrade gracefully when `/dev/uinput` is unavailable (they
log and continue), so parse+emit behavior is testable headless.

## DSL block

```hv
dsl {
    > "hello"          // send text
    : 100              // sleep 100ms
    $ "notify-send x"  // shell command
}
```

`dsl { }` sets an **input context**: the statement forms below are legal
inside it. Hotkey bodies (`F1 => { ... }`) get the same input context —
the forms work there too.

## Sending input

```hv
dsl {
    "hello"            // bare string = send text
    {Enter}            // send a single key
    {F1}               // any key name works
    lmb                // left mouse click
    rmb                // right mouse click
    mmb                // middle mouse click
    click()            // same as lmb
    click("right")     // same as rmb
    m(100, 200)        // move mouse to absolute (x, y)
    r(10, 20)          // relative move (dx, dy)
    w(0, 3)            // scroll: w(dy, dx); +dy scrolls down
}
```

Host bindings used: `io.sendKeys`, `io.sendKey`, `io.mouseClick`,
`io.mouseMoveTo`, `io.mouseMove`, `io.scroll`.

## Reading input state

```hv
dsl {
    < mouse            // io.mouseState()
}
```

`< keyboard` is not implemented (no host binding).

## Control flow sugar (input contexts)

```hv
dsl {
    * 3 { > "hi" }        // repeat 3 times    -> lowered to counted loop
    *? x < 10 { x = x + 1 }  // while sugar
    *: i in 0..3 { x = x + i } // for-in sugar (ranges are inclusive)
    ? x > 5 { > "big" }   // if sugar
    ?; x > 5 { > "big" }  // when-block sugar
    -> x                  // print(x)
    ;; repeats
    > "a"
    !!                    // re-emit the previous input command
}
```

The `repeat N { }` keyword form works anywhere, no `dsl {}` needed.

## Sleep

```hv
: 500       // milliseconds
```

Unit literals (`:1s`, `:1m30s`) are **not** implemented. Use `sleep()` or
`sleepUntil()` from the standard library.

## Not implemented (previously documented here, but never existed)

- `^{c}` `+{tab}` `!{f4}` `#{space}` modifier+key sends. `^`, `+`, `!`,
  `#` at statement starts are reserved for hotkey literals
  (`^c => { ... }`). To send combos, call `io.sendKey`/`io.sendKeys`
  with a combo string.
- `lmb_down` / `lmb_up` bare identifiers. Use `io.keyDown`/ or
  `io.mouseDown`/`io.mouseUp`.
- `>> file` / `<< file` (read/write files/config). Use the `fs` module.
- `<- x` ("return/break"). Use `return`/`break` in real control flow.
- `|>` pipeline. Pipelines use `|`.
- `& { ... }` fire-and-forget block, `data |> a |> b`, `{F1} .. {F12}`
  brace ranges, `on keydown klist {}`/`off keydown` (real form:
  `on keyDown(key1, key2) { ... }`, see docs/specs/Havel.md).

## Example

```hv
dsl {
    > "hello world"
    : 250
    {Enter}
    lmb
    : 100
    r(50, 0)
}
```
