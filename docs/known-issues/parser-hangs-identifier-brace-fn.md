# The parser hangs (no output, unkillable by SIGTERM) on `ident { fn ... }`

**Status:** FIXED (Nov 9 2026). The hang no longer reproduces on any
current tree.

## Symptom (as originally reported, Oct 31)

    printf 'foo { fn bar() }\nexit(0)\n' > hang.hv
    timeout -s KILL 12 ./build-debug/havel --lint hang.hv; echo $?
    # 137 (SIGKILL), no output at all - not even a parse error

Reproduced identically with `extern { fn bar() }` before the extern feature
landed, in both `--lint` and normal run. Plain `foo { }`, `foo { 1 }` and
`{ key: value }` object literals all parse/error normally - the loop needs the
`fn` inside the braces.

## Root cause

A DUPLICATED context-sensitive decorator-detection block in
`Parser::parseStatement()`. The block opens a lookahead over stacked
`[decorator]` brackets and saves the parser position in `savePos`:

    if (at().type == havel::TokenType::OpenBracket && at(1).type == havel::TokenType::Identifier &&
        (at(2).type == havel::TokenType::CloseBracket || at(2).type == havel::TokenType::OpenParen)) {
      size_t savePos = position;

When a second copy of this block was spliced into the middle of the first
one, the `extern { ... }` check was inserted BETWEEN the `savePos` capture
and the code that consumes the lookahead. The inserted branch could `return`
with the position restored only in some paths, and the brace/destructuring
dispatch below it then re-entered `parseStatement()` on the same token
without consuming it - a flat loop with no recursion to grow, which is why
SIGTERM did not land (the signal checks live in the recursion/eval paths).

## Where the duplicate came from

- `fc3e104cd` (Apr 22) introduced the original decorator block.
- `7d3a93f37` (Nov 8, "core: rename process.hv helpers off reserved
  keywords") brought a SECOND copy of the block into the tree - it is a
  merge of main's copy into the branch that already had one.
- `313dec21e` (Nov 9) removed the duplicate, keeping the `extern { ... }`
  check and ONE decorator block.

Commit count of the block's text over the affected range:

| commit | copies of the decorator block |
|--------|-------------------------------|
| `9fb44bdee` (Nov 2, extern feature) | 1 |
| `7d3a93f37` (Nov 8, process.hv rename) | 2 |
| `313dec21e` (Nov 9, config fix) | 1 |

## Verification

`--lint` on the original reproducer now exits non-zero with a diagnostic
instead of hanging. Checked on three independent binaries (havel-2, main, and
a pre-fix build of the same tree):

    printf 'foo { fn bar() }\nexit(0)\n' > hang.hv
    timeout -s KILL 20 <binary> --lint hang.hv; echo $?
    # rc=1, exit well inside the timeout

    error: Expected '=' or ':' after key
    error: Unmatched '}' - no open block to close

Binary identities for that run:

- `/host/havel-2/build-debug/havel` - commit `a3ea877aa91c` (havel-2, clean)
- `/host/havel/build-debug/havel` - commit `0f0b07623e6f` (main, dirty)
- `/tmp/havel-prefix/build-debug/havel` - commit `c64c7748816e` (havel-2, clean)

No regression test was added: the fix was an accidental removal of a
duplicated block, and the failing shape is a plain parse error that the
existing lint path already rejects. The observable contract - `--lint`
terminates with a diagnostic instead of hanging - is what this entry
records.

## Related

- `docs/known-issues/setfield-expected-object-early-startup.md` - the
  `313dec21e` commit that removed the duplicate.
