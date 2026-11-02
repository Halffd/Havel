# The parser hangs (no output, unkillable by SIGTERM) on `ident { fn ... }`

**Status:** OPEN, pre-existing. Found while adding the `extern { }` block
(Oct 31): scripting `extern { fn f() }` against a binary without the feature
did not error - it looped forever. The same shape under any identifier loops
too, so a typo can hang `--lint` and script runs.

## Symptom

    printf 'foo { fn bar() }\nexit(0)\n' > hang.hv
    timeout -s KILL 12 ./build-debug/havel --lint hang.hv; echo $?
    # 137 (SIGKILL), no output at all - not even a parse error

Reproduced identically with `extern { fn bar() }` before the extern feature
landed, in both `--lint` and normal run. Plain `foo { }`, `foo { 1 }` and
`{ key: value }` object literals all parse/error normally - the loop needs the
`fn` inside the braces.

## Leading hypothesis (UNVERIFIED)

The `OpenBrace` statement case (Parser.cpp, the object-literal /
destructuring / block dispatch) or the statement-recovery path re-enters
`parseStatement` on the same token without consuming it: the object-literal
lookahead (`isObjectLiteral()`) likely declines `fn`-led contents, the
destructuring lookahead declines too, and the block/statement path then tries
the same `{` again. No depth guard fires because no recursion grows - it is a
flat loop, which is also why SIGTERM does not land (signal checks live in the
recursion/eval paths).

## Next step

Trace the `OpenBrace` case for input `{ fn ... }` and assert forward progress
(the parser's `position` must advance each loop iteration; there are
DepthGuards for recursion but apparently no progress assertion for the
statement loop). Add a lint-only regression (`--lint` must exit non-zero with
a diagnostic within a timeout) once located.
