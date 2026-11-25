# Unknown member on a module namespace silently resolves to null

## Status

Open. Not fixed - changing the resolution semantics is a language-level
decision that would affect every module namespace, not a narrow bug fix.

## Symptom

A call to a host function that does not exist returns `null` instead of
raising an error. The mistake surfaces later, as a confusing failure in
whatever consumed the null:

```
pid = window.getPid()          # window.getPid does not exist
print(pid)                     # -> null
process.sendSignal(pid, process.SIGKILL)
                                # -> [ERROR] __proc.kill() requires a number PID
```

The reported error names the CONSUMER (`__proc.kill`), not the typo
(`window.getPid`). Nothing in the output mentions that the original call
resolved to nothing.

## Mechanism

`window`, `process`, `fs` and the other module namespaces are object
values. A member call on one goes through `CALL_METHOD`
(`src/havel-lang/compiler/vm/VMControlFlow.cpp`):

1. The receiver is an object id, so the direct-field lookup runs
   (`instanceObj->find(method_name)`).
2. The name is absent, so `found_host` stays false and `vm_func` stays
   null.
3. The prototype and module-monkey-patch lookups also miss.
4. The unresolved arm pops the args and pushes `Value::makeNull()`.

So an unknown member is indistinguishable from a member that legitimately
returns null.

## Why it matters

The null is the only signal, and it is a weak one. A typo, a renamed
host function, or a function that was never wired into the bridge all
look the same, and the resulting error points at the wrong place. This
is how `window.getPid()` (never a host function - the documented API is
`window.pid`) reached a `__proc.kill()` type error.

The same shape applies to `window.getProcess()`, which appears in older
user scripts but has never been registered.

## Reproducer

```
// scripts/tests/unknown_member_returns_null.hv (hand-run; not a smoke
// test - it asserts the CURRENT, undesirable behaviour)
pid = window.getPid()
print(pid)              // null, no error
```

## Relevant source

- `src/havel-lang/compiler/vm/VMControlFlow.cpp` - the `CALL_METHOD`
  unresolved arm that pushes null.
- `src/havel-lang/compiler/vm/VMCb.cpp` - the JIT-side mirror of the
  same dispatch; any fix has to land in both to keep tier-1 parity.
- `src/havel-lang/compiler/core/HostFunctionNames.generated.hpp` - the
  generated registry of real host function names, the source of truth for
  what exists.

## Next step

Decide the language's stance, then implement it in both the interpreter
and the JIT bridge:

- **Error on an unknown member of a module namespace.** The namespace's
  member set is known and enumerable (the generated registry above), so
  an unknown name is a definite authoring error. This is the strictest
  option and would have caught `window.getPid()` at the call site.
- **Return null but warn.** Weaker; still surfaces the typo, but the
  null still leaks downstream.

Either way the change needs a tier-1 parity check
(`scripts/ci/jit-parity-test.sh`), because `VMCb.cpp` mirrors the
dispatch and a one-sided fix would diverge interpreted and tiered runs.

## Related

- `docs/reference/host-functions.md` - the list of real window/process
  host functions.
