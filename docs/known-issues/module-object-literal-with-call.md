# An object literal whose field contains a module-local call evaluates to that call's value

**Status:** FIXED in `ByteCompiler.cpp`. Root cause confirmed, fixed, and
the fix verified three ways (see "Verification"). No regression test is
landed — "Why there is no test" records what stopped it and what a test
would need.

## Symptom

A module function whose last expression is an object literal containing a
call to a function defined in the same module returns that call's value
instead of the object:

    // mymod.hv
    fn helper() { 7 }
    fn withCall() {
        ({
            a: 1,
            b: helper()
        })
    }

    // consumer
    use mymod
    print(str(mymod.withCall()))   // prints 7, not {a: 1, b: 7}

It is silent. The caller gets a number where an object is expected, so the
failure surfaces later as "Attempted to read property X of int" or a null
field, far from the literal that lost its shape.

## Evidence

Minimal reproducer, run as a module (`use mymod` from an entry script):

    fn helper() { 7 }
    fn withCall()           { ({a: 1, b: helper()}) }
    fn withCallParen()      { ({a: 1, b: (helper())}) }
    fn withCallOneLine()    { ({a: 1, b: helper()}) }
    fn withStrInterp()      { _d = "/tmp"; ({a: 1, b: "${_d}/x"}) }
    fn withHostCall()       { _d = "/tmp"; ({a: 1, b: fs.read("${_d}/x")}) }

Observed:

    withCall          = 7        WRONG (expected {a: 1, b: 7})
    withCallParen     = 7        WRONG
    withCallOneLine   = 7        WRONG
    withStrInterp     = {b: /tmp/x, a: 1}    correct
    withHostCall      = {b: null, a: 1}      correct

What rules the shape in or out:

- a call to a function defined in THIS module: broken
- a call to a host module function (`fs.read`): correct
- string interpolation: correct
- the same literal inside `if/else`: correct
- a module function called as a STATEMENT with variable arguments
  (`_m = _timeLeft(a, b, c, d, e)`): correct
- the same literal as the entry script's last expression: correct

So the trigger is specifically a module-local call appearing inside an
object-literal field. A second, narrower symptom came out of the same
investigation and is fixed separately: `float()` was not callable at all,
because `NumberPrototype.cpp` registers `float.abs`/`float.round`/... and
`VM::buildNamespaceGlobals()` synthesised a `float` namespace object that
won the LOAD_GLOBAL lookup. That one is fixed in the tree by giving `float`
a bare registration like `int` and `num` have.

## Where it is

The parser/compiler turns `({a: 1, b: helper()})` at statement level into
something that evaluates the last call and discards the literal. Not yet
localised to the exact rule in the parser - the expression-position form
inside `if/else` is correct, which points at the statement-level object
literal path rather than the object-literal expression itself.

## Workaround in-tree, and why it is the right shape anyway

Every module that returns an object computes its values into locals first
and the returned literal names only those locals:

    fn _readBattery(name) {
        _percent = _num("${_dir}/capacity")
        _status = _text("${_dir}/status")
        ({name: name, percent: _percent, status: _status})
    }

`modules/app/monitor.hv` already does exactly this (`monitor.cpu()` returns
a literal of locals only), so the workaround is the codebase's own
convention rather than a special case. `modules/app/laptop.hv` follows it
and says so in its header.

## Next step

Fix the statement-level object literal so a call in any field keeps the
literal's shape, matching the expression-position behaviour that already
works. The reproducer above is the whole test - wrap it as a smoke test
that asserts `withCall() == {a: 1, b: 7}` and it fails until the parser is
fixed. Until then, any new module that returns an object must follow the
locals-first convention.
---

## Fix

`ByteCompiler.cpp`'s `ArrayLiteral`, `TupleExpression`, `SetExpression`
and `ObjectLiteral` cases each compiled an element (or field) expression
without clearing `in_tail_position_`. Every other sub-expression context in
that switch does exactly this (`IfExpression` at the same site is the
clearest example), so the four collection literals were the omissions, not
a missing feature.

Each case now saves the flag, sets it false for the duration of the element
compile, and restores it. Nothing else changes: a call in genuine tail
position still emits `TAIL_CALL` and `RETURN`.

## Workaround removed

The locals-first convention (`monitor.hv`, `laptop.hv`, the laptop module's
header note) is no longer required for correctness. It is kept because it
reads well, not because it avoids a bug. The reference to it in
`modules/app/laptop.hv`'s header is still accurate as documentation of
style.

## Verification

Three independent checks, all against `/builddir/havel-dev/havel`:

1. **Bytecode.** `fn callTail() { ({a: 1, b: seven()}) }` now emits
   `LOAD_GLOBAL seven` / `CALL` / `LOAD_CONST b` / `OBJECT_SET` — the
   `RETURN` that used to sit between them is gone, and the last
   `OBJECT_SET` is reachable again. `fn tailCall() { seven() }` still emits
   `LOAD_GLOBAL` / `TAIL_CALL` / `RETURN`.
2. **The original reproducer**, a module with one function per literal kind:
   `[helper()]` -> `[7]`, `({a: helper()})` -> `{a: 7}`,
   `[[helper()]]` -> `[[7]]`, `({a: {b: helper()}})` -> `{a: {b: 7}}`
   (all four returned the bare number before the fix), while literals with
   no calls still return the literal.
3. **Tail-call optimisation survived**: `fn countdown(n, acc)` recurses
   200,000 deep and returns the right answer, which it cannot do without
   `TAIL_CALL`.

## Why there is no test

A regression test was written and landed as
`scripts/smoke/test_issue_object_literal_tail_call.hv` (fixture module
written to a temp dir, compiled and run by a child process with a sandboxed
HOME and `XDG_CACHE_HOME`, asserting on the child's output) and it
demonstrated the defect correctly — the A/B was clean: 4/4 FAIL with the
guards reverted, and the same shape PASSes with them in. It then proved
non-deterministic in this chroot in both directions.

The cause is a bytecode cache the child cannot be kept away from:
`ModuleLoader::getCacheDir()` prefers an existing "additional cache
directory" over `getDefaultCacheDir()`, the fixture's `sys.pid()` is 1/2/3
in the container so a pid-named fixture dir is reused across runs, and the
cache key is the module's content hash, so a module compiled by one binary
is read back by the next. The test was fixed against all of that it could
reach (a `time.now()` component in the fixture path, `fs.removeAll`
cleanup, `XDG_CACHE_HOME` into the sandbox) and still flapped.

Shipping a test that fails at random is worse than shipping no test, so it
was removed rather than committed. What a working test needs:

- a cache knob that reliably disables the module bytecode cache for the
  child (there is no such env var today), or
- the fixture module committed as a real file that smoke does not collect
  (`run_smoke_suite` takes every `scripts/smoke/*.hv`, so a library
  fixture cannot sit beside its test), or
- a way to assert on the emitted bytecode — a disassembler over
  `havel --build` output — which needs no child process and no cache.

Until one of those lands, the three checks above are the verification of
record, and the reproducer in "Symptom" is the fastest way to re-check.
