# A method call on a module-level variable fails; the same call on a parameter works

**Status:** OPEN — root cause pinned with a minimal reproducer; worked
around in-tree in `modules/app/monitor.hv`, not fixed.

Found while documenting `modules/app/monitor.hv`: `monitor.cpuPerCore()`
returned an empty array on a 4-core machine, which sent me into the
`/proc/stat` parsing rather than the name resolution, where the bug
actually is.

## Symptom

Inside a module, calling a string method on a variable that lives in the
module's global scope fails. The same call on a function parameter works:

```hv
// probe.hv  (loaded with `use probe`)
fn viaParam(s)        { s.find("cpu ") }                    // works
fn viaAssign(lines)   { _l = lines[1]  _l.find("cpu ") }     // FAILS
fn viaForLoop(lines)  { _out = []; for _l in lines { _a = _l.find("cpu "); _out.push(_a) } _out }
```

Run against `/proc/stat`'s first per-core line (`"cpu0 19573 0 6615 ..."`):

    viaParam  = -1                                  correct
    viaAssign = TypeError: Attempted to call non-callable value of type string
    viaForLoop = [0, 0, 0, ...]                     WRONG - every probe reports 0

Two different failures from one cause: `viaAssign` throws, and the for-loop
variant silently gets `0` back for every `.find`, which is why
`cpuPerCore()`'s `_l.find("cpu") == 0` never matched and the array came
back empty. Both are why a loop over `/proc/stat` looked like a parsing
problem for an hour.

What matters is where the receiver was bound, not how the call is written —
`_l.find("cpu ")` and `s.find("cpu ")` are the same expression:

| Receiver binding | Result |
|---|---|
| function parameter | correct |
| assigned from an array index at module scope | throws |
| for-loop variable at module scope | silently returns 0 |

The identical expression at the entry script's top level is correct too, so
this is a module-scope name-resolution problem, not a string-method
problem.

## Where it is

The receiver's `.find` resolves against the module's global scope and finds
a binding that is not the string prototype method — the error says the
callee is a **string**, which points at a global named `find` being picked
up as the method. `modules/app/monitor.hv` does `use string`, and
`modules/std/string.hv` exports `fn find(s, sub, start)`, so that is the
first place to look: a module that imports `string` gets `find` in scope,
and a receiver that is not a plain local picks that up instead of the
prototype method.

## Workaround in-tree

Pass the value through a function so it arrives as a parameter:

```hv
fn _isCpuCoreLine(line) { line.startsWith("cpu") && !line.startsWith("cpu ") }
...
for _l in _lines {
    if _isCpuCoreLine(_l) { ... }
}
```

`cpuPerCore()` works again (4 cores, `{total, idle}` each). The same trick
is already how `modules/app/monitor.hv` reaches `string.join` and
`string.replace` — those take the string as an argument, so they never hit
this.

## Next step

Fix method dispatch so a receiver in module global scope resolves to the
prototype method, not to an imported module function of the same name. The
reproducer above is the whole test: assert `viaAssign(["cpu0 1 2 3"])` ==
viaParam's answer and that the for-loop variant returns the same, and both
fail until dispatch is fixed. Until then, any module that calls a method on
a module-level variable must pass it as a parameter.