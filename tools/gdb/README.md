# Havel GDB helpers

Inspect Havel runtime values (`havel::core::Value`) from GDB.

Native `hvdb` remains the primary debugger. These helpers are for when you are
already stopped inside the real `havel`/`hvdb` process and want to read a
`Value` without switching tools.

## Load

From the repo root:

```
(gdb) source tools/gdb/havel.py
```

Or use the loader, which searches the current directory and its parents:

```
(gdb) source tools/gdb/load-gdb-printers
```

## Commands

| Command | Purpose |
|---------|---------|
| `havel-value <expr>` | Decode one `Value` from its NaN-boxed bits |
| `havel-vector <expr>` | Decode every element of a `std::vector<Value>` |
| `havel-instruction <expr>` | Show opcode, source location, and operands |
| `havel-hostfn <expr>` | Show a `HostFunctionInfo`: name, module, arity, flags |
| `havel-vm [expr]` | Point host-function name resolution at a `VM` |
| `havel-str <expr>` | Resolve a string `Value`'s text through the VM |

### Point at the VM first

Host-function `Value`s store an index, not a name. Tell the module where the
`VM` is so indices resolve to registered names:

```
(gdb) havel-vm this
havel-vm: this (havel::compiler::VM)
(gdb) havel-value value
fn string.chr [1]
```

`this` works from inside any `VM` method. `havel-vm vm` works too, for a VM
that is a local rather than a receiver; references and pointers are all
accepted.

`havel-str` needs the same setup, because resolving string text reads the VM's
chunk tables:

```
(gdb) havel-vm this
(gdb) havel-str value
some string contents
```

## Why commands and not pretty-printers

There are no registered `gdb.pretty_printers` here. On GDB 18.1:

- A catch-all printer subclass degraded ordinary `print` output for ints,
  pointers, and unrelated structs.
- `lookup_typedef`/lookup-function based printers were invoked but rendered
  empty values.
- `format_string(raw=True)` exposed libstdc++ internals instead of Havel data.

Explicit commands decode on demand and leave normal `print` untouched. Verified:
with this module loaded, `print` and `print/d` produce byte-identical output for
non-Havel types.

## Deliberately not covered

`BytecodeHostFunction` is a bare `std::function<Value(const std::vector<Value>&)>`
— there is no metadata in it to decode, and its internals are libstdc++
implementation detail.

The host-function registry is not dumped here either. `hvdb` already does it
natively and in more detail: `hostfuncs [filter]` lists the registry and
`hostfunc <name>` shows index, module, arity, namespace, callable and global
binding. A GDB command for the same data would be a second implementation of
something that already ships.

## Safety

All decoding reads the private `bits_` field and plain data members. No
inferior calls, so a decode cannot deadlock the target or re-enter GDB mid-print.
`havel-str` is the one exception: it deliberately calls
`VM::resolveStringKey`, which re-enters the debugger. It cannot be used while
already stopped inside that function.

`VM::resolveStringKey` is only callable because it is defined out of line in
`VMValue.cpp`; an inline body cannot be called from a GDB expression.

Two GDB expression-parser limits are worth knowing:

- Nested calls are not supported, so `havel-str` needs a `Value` that already
  exists in a variable or frame. `havel-str Value::makeStringValId(7)` fails
  with a syntax error; `havel-str value` works.
- `VM::toString` and `VM::isTruthy` are inlined into their callers, so a
  breakpoint there has no `this`. `VM::resolveStringKey` is the one `Value`
  entry point that reliably survives as a frame — which is also why `havel-str`
  cannot be used while stopped inside it.

## Layout is mirrored, not shared

`havel.py` hardcodes the NaN-boxing constants from
`src/havel-lang/core/Value.hpp`. GDB cannot include project headers in a
pretty-printer context, so the layout is duplicated deliberately.

**If `Value.hpp` changes its encoding, change `havel.py` in the same commit.**

`value_probe.cpp` mirrors the same layout independently and `test_printers.sh`
asserts the decoding, so a change to one without the other fails the test.

## Tests

```
tools/gdb/test_printers.sh
```

Builds `value_probe.cpp`, runs every command in a batch GDB session, and asserts
each decode. Exits non-zero on any mismatch. Honors `CXX` and `GDB`.
