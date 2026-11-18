# An object literal whose field contains a module-local call evaluates to that call's value

**Status:** OPEN — root cause confirmed with a minimal reproducer; the
defect is worked around in-tree, not fixed. Regression test that pins the
workaround's shape: `scripts/tests/test_laptop_module.hv` (it asserts the
laptop module's derived numbers, which only hold when the literals carry
variables).

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