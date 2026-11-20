# `Module std/config failed to parse: Unexpected token in expression: << / == / >>`

**Status:** NOT REPRODUCIBLE on the current tree. The failing file was an
uncommitted, in-flight working-tree edit; the committed module parses.
A parse error of this shape means "the file on disk is malformed", not
"the language lacks the operator" - see the evidence below.

## Symptom (user's log, Nov 8 2026 02:22:33)

    [ERROR] Execution error: Module std/config (/home/all/repos/havel/modules/std/config.hv)
      failed to parse: Unexpected token in expression: <<
      Expected ';' or newline after expression (Havel requires statement terminators)
      Unexpected token in expression: ==
      Expected ';' or newline after expression (Havel requires statement terminators)
      Unexpected token in expression: >>
      Expected ';' or newline after expression (Havel requires statement terminators)
    [/host/havel/src/havel-lang/compiler/vm/VM.cpp:7134]

Three errors in a row for one expression (`<<`, then `==`, then `>>`) are
the parser's error recovery walking a single malformed expression, not
three missing features. The throw site is
`src/havel-lang/compiler/vm/VM.cpp` - the `SourceCompileStatus::ParseFailed`
arm of `VM::loadModule`.

## Evidence that it was a transient working-tree edit

1. NO committed version of `modules/std/config.hv` contains `<<` or `>>`.
   Checked every version in history: `649a3ee7f`, `6a70e0063`,
   `d3e21ce7b`, `4a148d99a`, `0e4a84b19`, `0df2a84ed`, `fa84661a6`,
   `cb1d63d0e`, `f33b38fc4`, `e8b9c8f5c` - all zero shift tokens.
2. Infix `<<`/`>>` HAVE been supported by the C++ parser long before
   Nov 8 (`Parser.cpp`, `case TokenType::ShiftLeft:` -> `ast::BinaryOperator::BitwiseShiftLeft`,
   with `ByteCompiler.cpp` mapping it to `OpCode::BIT_LSH`). So this was
   never a missing-operator language gap. `==` is a plain comparison
   (`case TokenType::Equals:` -> `BinaryOperator::Equal`).
3. The file's mtime is 2026-11-08 05:47, i.e. 3h25m AFTER the 02:22
   failing run, and the file is now byte-identical to HEAD (the working
   tree is clean for that path). The edit that broke the parse was
   replaced before it was committed.
4. The retired self-hosted copy (`out/modules/std/config.hv`, Oct 25) has
   no shift tokens either.

## Current state - the committed module parses

    env -u DISPLAY -u WAYLAND_DISPLAY -u XAUTHORITY -u XDG_RUNTIME_DIR \
      HAVEL_HEADLESS=1 /host/havel/build-debug/havel <probe.hv>
    # probe.hv:
    #   use "std/config" as cfg
    #   print("config module loaded; get() returned: " + cfg.get("Probe.MissingKey", "fallback"))
    #   sys.exit(0)

    -> config module loaded; get() returned: fallback
    -> no "failed to parse", no "Unexpected token in expression"

Binary: `/host/havel/build-debug/havel`, self-reporting
`commit 3ef0fa46ca63 (main, dirty)` = HEAD `3ef0fa46c`.

## If this shape reappears

The error names the exact file, so read THAT file (not the importing
script) and look for the malformed expression. `<<`/`>>` are valid
infix operators in Havel; the parser complains when an expression is
left unterminated, so the real defect is usually a missing operator or
a missing separator NEAR the reported token - the reported token is
where recovery stopped, not necessarily where the mistake is.

## Related

- `docs/known-issues/setfield-expected-object-early-startup.md` - the
  other `std/config` defect in the same cluster (the module-scope
  `config {}` / `cfg.set()` crash).
- `docs/known-issues/cfg-section-chain-shell.md` - the `std/config`
  binding defect chain.
- `docs/known-issues/hotkey-binding-external-root-pin-leak.md` - the GC
  pin leak in the SAME user log (263 roots still pinned at shutdown).
