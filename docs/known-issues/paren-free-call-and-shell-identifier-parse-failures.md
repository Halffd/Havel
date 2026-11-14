# Paren-free calls and bare-identifier shell commands failed to parse

**Status:** FIXED (Nov 13 2026). Four distinct defects in the C++ parser and
lexer, all reached from ordinary REPL input.

A fifth defect was introduced while fixing these and caught by the smoke
gate: enabling nested call sugar in the Pratt tail block made an `on`
event's `where` filter swallow the handler body. See section 7.

## Symptom

The REPL transcript showed these failures, each one a hard parse or semantic
error on input that the language documents as valid:

| input | error before the fix |
|---|---|
| `print mode` | `Expected ';' or newline after expression` |
| `print replace "a", "b"` | `Expected ';' or newline after expression` |
| `_v \| replace "l", "L"` | `Expected '=' after comma-separated targets` |
| `$ firefox` | `SemanticError: Unresolved identifier 'firefox'` |
| `:1s` | `sleep(): invalid duration format` |

## Root causes

### 1. `print mode` - keyword arguments were not call arguments

`Parser::parsePrattExpression` recognises a paren-free call by looking at the
token AFTER the callee. The accepted set listed only literals and a few
prefix operators; the keywords that the language allows in expression
position (`mode`, `class`, `config`, ...) were not in it, so the keyword was
never consumed as the argument it is. The statement parser then hit the
keyword and reported a missing terminator.

Fix: the accepted set is now one shared predicate,
`canStartImplicitCallArgument()` in `Parser.h`, used by both paren-free call
sites. The keyword half of that set is derived from
`keywordUsableAsIdentifier()` - the same list the Pratt parser's `nud` uses
to decide which keywords may start an expression - so the two can never
disagree. (The parser's other keyword list, `isKeywordToken()`, is a
different concern: it covers NAME positions - module names, type names,
parameter names - which legitimately accept a broader set than expression
positions.)

The set must NOT contain the binary operators `+`, `-` or `~`, nor the
spread token `...`. Adding them looked harmless but broke every expression
with a binary operator: in `val x = -x + 52` the parser read the `+` as the
start of a paren-free call argument and built `x(+52)`, so `x` (an int) was
called - "TypeError: Attempted to call non-callable value of type int".
Nine smoke tests failed this way (arithmetic_negate,
const_tuple_destructure, math_sidecar_randint, closure_loop_capture, ...).
The prefix operators that DO belong in the set are `@`, `@@`, `!` and the
length operator `#` (`TokenType::Length`).

### 2. `print replace "a", "b"` - the tail block disabled nested call sugar

The tail block of `parsePrattExpression` parsed its argument with
`context.allowBraceSugar = false`. That flag is what recognises a nested
paren-free call, so `print replace "a", "b"` parsed the argument as the bare
identifier `replace`; the call was never formed and the leftover tokens hit
the statement parser.

Fix: nested call sugar stays enabled. The sugar is right-associating (an
argument binds tighter than its caller), so the recursion terminates on the
token stream and the parser's depth guard bounds it.

### 3. `_v | replace "l", "L"` - the postfix call site stole the argument

`Parser::parsePostfixExpression` formed a single-argument call and returned,
leaving the comma to the caller. The caller was the assignment-expression
parser, whose comma branch is the MULTI-ASSIGNMENT path (`a, b = ...`); it
reported "Expected '=' after comma-separated targets" for a plain
two-argument call.

Fix: the postfix call site now absorbs the comma-separated argument list, the
same way the tail block does.

### 4. `$ firefox` - a bare identifier was a variable read, not a command

`docs/specs/Havel.md` documents `$ firefox` as "Fire-and-forget execution",
but the parser turned the bare identifier into an identifier EXPRESSION, and
the compiler compiled it as a variable read. With no variable named
`firefox`, the script died with "Unresolved identifier 'firefox'".

Fix: the bare identifier is wrapped in a string literal, so it names the
command to run. A variable holding a command is still reachable through the
explicit `$! (var)` form.

### 5. `:1s` - the lexer destroyed the unit suffix

This one is a LEXER defect, not a parser defect, and it was the reason the
parser-side duration fix alone did not work.

The lexer's regex-literal heuristic listed `TokenType::Colon` as a regex
context. After the `:` of a sleep duration, the unit suffix was therefore
scanned as a REGEX LITERAL (up to the next `/` or EOF) and silently dropped:
`:10ms` produced the token stream `Colon`, `Number "10"` - the `ms` never
became an identifier. The parser then handed the bare number `10` to
`sleep()`, which rejected it with "sleep(): invalid duration format".

Fix: a colon is not regex context. A colon introduces a sleep duration, a
slice bound or a label - never a regex literal.

The parser side needed a matching fix: the unit suffix is a SEPARATE token
(`Number "1"` + `Identifier "s"`), so the sleep parser re-joins a suffix
that is directly adjacent to the number. `:1 s` (with a space) keeps the
separate-token behaviour.

A third fix lives in `VM::parseDuration`: the single-unit regex rejected the
compound forms the spec lists (`1h30m`, `1m30s`), so `sleep("1h30m")` failed
with the same "invalid duration format" error. Compound durations are now
summed from their unit components.

### 6. `LOAD_CONST` emitted a value where the dispatch expects a pool index

With the AST now carrying the right duration string (`SleepStatement{duration:
10ms}`), the runtime still rejected it. The cause was in the BYTECOMPILER:

    emit(OpCode::LOAD_CONST, Value::makeStringValId(strId));   // WRONG

`LOAD_CONST` takes a CONSTANT-POOL INDEX as its operand - `VMDispatch.cpp`
reads `instruction.operands[0].asInt()` and indexes the pool. Emitting the
value itself made the operand a string, so the pool index was garbage and
`sleep()` never received the duration string.

Every other string constant in the compiler goes through `addConstant()`:

    emit(OpCode::LOAD_CONST, addConstant(Value::makeStringValId(strId)));

Three other sites had the same defect and are fixed the same way:

- the set-literal element marker (`SET_SET` pushes `true`)
- a `when` block with no condition
- the DSL input statement's inline sleep duration

None of the three had an OBSERVABLE symptom, for different reasons:

- the set-literal marker: the set API (`len`, `has`, `add`, `delete`,
  `list`, `union`, ...) exposes only the set's KEYS, never the stored VALUE,
  so the corrupted marker was invisible to scripts.
- the `when` block without a condition: both `WhenBlock` construction sites
  (`parseWhenBlock` and `parseDslWhenBlock`) always set a condition, so the
  branch is defensive-only.
- the DSL inline sleep: `getConstant` bounds-checks its index and returns
  null for an out-of-range slot, and the garbage index happened to resolve
  to a slot holding the same integer - the measured DSL sleep duration was
  identical before and after the fix (1500ms both ways).

So these three were latent defects: correct only by accident of the
constant-pool layout, not by construction. The fix makes them correct by
construction. No regression test was added for them because there is no
consumer-visible behaviour to assert - asserting the constant-pool layout
would be testing an implementation detail.

The codebase has TWO `LOAD_CONST` implementations with different operand
semantics - `VM::executeInstruction` (VMDispatch.cpp) reads a pool index,
while `VMExecutionContext::executeInstruction` (VMExecutionContext.cpp)
pushes the operand directly. The compiler emits for the first one, so the
`addConstant()` form is the correct one.

`LOAD_GLOBAL` and `STORE_GLOBAL` are different: their operand IS a
StringValId, checked explicitly by the dispatch
(`if (!instruction.operands[0].isStringValId()) COMPILER_THROW(...)`), so
those emissions were already correct and are unchanged.

### 7. REGRESSION: nested call sugar made an `on` filter swallow the handler

This defect was introduced by fix 2 (nested call sugar stays enabled in the
Pratt tail block) and caught by the smoke gate, not by the targeted
regression tests.

`Parser::parseOnEventParts` parses an optional `where` filter and then
expects the handler body to be `{`:

    on "filtered.event" where event.kind == "special" {
      special = 1
    }

Before fix 2, the tail block disabled `allowBraceSugar` while parsing its
argument, so the `{` after the filter's trailing string was never taken as
a brace-call argument. With nested sugar enabled, the filter expression
`event.kind == "special"` was followed by `{`, and the string took the
handler block as a brace-call argument. The parser then found no `{` for the
handler body and reported:

    Parse error: Expected '{' after event subscription

Fix: the filter expression is parsed with `allowBraceSugar` off - the same
guard the other expression-then-block constructs use (`repeat`, `for`,
`while`, ...). The guard is local to the filter; the handler body is parsed
after the flag is restored.

`scripts/smoke/test_event_bus.hv` is the regression test for this: it
covers both the `where` filter and the handler body that follows it.

### 8. The smoke gate's debug-build timeouts are not caused by the parser

Several gate runs reported `(timeout)` failures that have nothing to do
with the parser changes:

- `test_debug_on_off`, `test_fs_glob_absolute`,
  `test_issue_gc_cursor_string_sweep` - each carries a `smoke: timeout`
  of 30-60s, and the debug build's ASAN/UBSAN instrumentation makes even a
  trivial `exit(0)` script take ~43s against ~5s for the release build.
- The two regression tests added for this fix initially carried
  `smoke: timeout = 30` and were themselves killed by the same overhead
  (measured at 24s and 31s). They now carry 120s, matching the other
  tests that do real work.

A prior gate log in this workspace (`test-havel-stable-build-debug-gate.log`,
Nov 10) already shows the same pattern - four timeout failures on an
unrelated tree - so the debug build's timing sensitivity predates these
changes.

`test_issue_async_global_accumulation` failed once in the gate with
"received values in order: expected [0, 1, 2, 3], got [0, 1, 3, 2]". The
test races goroutines against 10ms sleeps, so the order depends on
scheduling. It passed 3/3 standalone and 8/8 with eight concurrent runs on
the fixed binary, and 5/5 on the pre-fix binary. It is a timing flake, not
a parser regression - the parser changes affect compile time, not
scheduling.

## Verification

Each defect was reproduced against a pre-fix binary and re-run against the
fixed one:

    # pre-fix: /host/havel/build-release/havel (commit 9ad6a2fe2808)
    $! pwd        -> SemanticError: Unresolved identifier 'pwd'
    _v | replace  -> Parse error: Expected '=' after comma-separated targets
    # post-fix: /host/havel/build-debug/havel (commit 18fe53856b4f)
    $! pwd        -> cwd=/tmp exit=0
    _v | replace  -> heLlo

Regression tests:

- `scripts/smoke/test_issue_repl_parenless_and_shell_forms.hv`
- `scripts/smoke/test_issue_sleep_duration_suffix.hv`

Both pass against `/host/havel/build-debug/havel` (commit `18fe53856b4f`).

Widening the accepted argument set to the binary operators `+`, `-` and `~`
was an intermediate regression: nine unrelated smoke tests then failed with
"TypeError: Attempted to call non-callable value of type int" because
`a + b` was parsed as `a(+b)`. Removing those operators from the set fixed
all nine. Verified against the same binary:

    arithmetic_negate, await_cofn_multi_yield, chunk_navigator,
    closure_loop_capture, closure_multiple_upvalues, closure_shared_upvalue,
    const_tuple_destructure, math_sidecar_randint, negative_number
    -> all PASS (no Bytecode error, no Parse error)

The forms themselves, checked against the same binary:

    print replace "hello", "l", "L"  -> heLlo
    "hello" | replace "l", "L"        -> heLlo
    string.replace "hello", "l", "L"  -> heLlo

The lexer change (a colon is not regex context) was checked against the
other colon-introduced constructs, so the change does not silently break
them:

    ternary `5 > 3 ? "yes" : "no"`    -> yes
    slice `[1,2,3,4,5][1:3]`          -> [2, 3]

The four `LOAD_CONST` sites were checked individually:

    set literal  {1, 2, 3}            -> len=3, has(1)=true, has(9)=false
    DSL inline sleep `:1500`          -> --lint exits 0 (no side effects)

## Related

- `docs/specs/Havel.md` - the `$ command` and sleep-duration contracts that
  the code now matches.
- `SYNTAX.md` - the paren-free call section, updated with the
  comma-separated argument form.
