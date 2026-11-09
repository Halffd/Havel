---
plan name: self-hosted-transition
plan description: Full transition to the Havel-implemented compiler (modules/lang), with measured baseline and milestone gates
plan status: active
related: bootstrap-self-host.md, execution-path-map.md, specs/bootstrap-compiler.md
---

# Self-Hosted Transition Plan

Completes `bootstrap-self-host.md`: the C++ frontend (Lexer, Parser,
SemanticAnalyzer, ByteCompiler) becomes deletable once the Havel compiler in
`modules/lang/` handles the language the repo actually ships. The VM, GC,
fiber scheduler, FFI, and host module system stay C++ permanently
(`specs/bootstrap-compiler.md`).

## Baseline (measured 2026-10-24)

Tool: `scripts/ci/selfhost-coverage.sh` — lints every `.hv` in the repo
through the launcher (`--lint`: tokenize → parse → infer; side-effect-free).
Tree: `7224c8df2` + the launcher `--lint` arg fix (see M0), build-nollvm,
interpreter (no JIT), default GC budget.

**1481 / 1558 files parse+infer (95.0%). 77 fail.**

| Failure family | Count | First repro | Root region |
|---|---|---|---|
| Parse-error (grammar gaps) | 65 | `modules/std/semaphore.hv` "Unexpected token in expression" | `modules/lang/pratt.hv` |
| Null-key OBJECT index at bytecode offset | 11 | `modules/lang/libtest.hv` (53 lines) | `modules/lang/emitter.hv` or `types.hv` (error surfaces while the .hv pipeline itself runs) |
| GC root hole — `ARRAY_GET unknown object id` | 1 | `modules/lang/lexer.hv` | C++ VM/GC (see Blockers) |

Parse-error sub-families (from the sweep log, deduplicated messages):
`Unexpected token in expression` (27), `Expected identifier, '[' or '{'
after declaration` (11), `Expected '{' or ':' for block` (6), arrow-function
parameter forms (4), `Expected object key` (3), `for` clause variants (3),
`fn`-as-parameter (2), hotkey arity forms (2), plus singles: `on` message
forms, `co` spawn, `))` token, `with` block, null parameter name.

## Milestones

### M0 — lint tooling is truthful (DONE 2026-10-24)

`launcher.hv`'s `--lint`/`--format` handlers did `i = i + 1`, skipping the
file path: every `--lint` invocation tokenized an empty program and printed
"Linting successful" — including for syntactically broken files. Fixed in
the launcher (the path falls through to the scriptFiles branch).
`scripts/ci/selfhost-coverage.sh` added; run it with a baseline argument to
pin a floor in CI once failures reach zero.

### M1 — parse parity: 1558/1558

Fix the 65 grammar gaps in `pratt.hv`, using each failing file as its own
test (`--lint <file>` must print `Linting successful`). Work in construct
order, not file order: one family (e.g. "Unexpected token in expression")
typically unlocks many files. Gate: `selfhost-coverage.sh` exits 0 on parse
errors only; emit/GC failures tracked separately.

Deliverable detail: add per-family regression snippets to
`modules/lang/`'s own test dir so a pratt fix cannot regress another family.

### M2 — pipeline correctness: the 12 non-parse failures

1. **Null-key OBJECT index (11 files).** Minimal repro
   `modules/lang/libtest.hv` lints → `OBJECT index expects
   string/number/bool key (got null)` at bytecode offset 247:21. The error
   is thrown while the .hv pipeline itself executes (the launcher/linter
   does not run the target), so the defect is inside pratt/types/emitter
   processing of some construct libtest.hv uses (single-line `if ... :
   return ...` is the prime suspect). Bisect libtest.hv down to the
   smallest failing snippet, fix in the responsible module.
2. **GC root hole (lexer.hv).** Deterministic: `--lint
   modules/lang/lexer.hv` dies with `ARRAY_GET unknown object id`;
   `--gc-budget 2000000000` (collections suppressed) parses in 750 ms and
   passes. Some live value's only reference is in a storage the root scan
   misses. Root set today: operand stack, full `locals` vector, active
   `globals`, active closure ids, open-cell slot reads, scheduler roots.
   Suspects to instrument, in order: open-upvalue cells whose closures are
   unreachable (box map holds the only shared_ptr), `globals_stack_`
   pushed sidecar maps (only active `globals` is scanned),
   `immutable_locals_`. See Blockers — the boxed-upvalues work below
   changes this same area.

### M3 — self-parse of modules/lang (the deletion criteria begin)

Per `specs/bootstrap-compiler.md`:
1. `lexer.hv` tokenizes `modules/lang/*.hv` → token stream identical to
   C++ `Lexer.cpp` (write a diff harness; Lexer.cpp stays until zero diff).
2. `pratt.hv` parses `modules/lang/*.hv` → AST round-trips (the M1 gate is
   the strict subset; here the AST must survive `str()` comparison against
   the native parse).
3. `types.hv` infers all of `modules/lang/` without error.
4. `emitter.hv` emits bytecode for `modules/lang/*.hv` → the emitted chunk
   executes identically to the natively compiled chunk (run-parity harness
   on the smoke suite).

Gate: `scripts/ci/selfhost-parity.sh` (to be written in M3) — for N sample
scripts, native-compiled vs .hv-compiled bytecode produce identical
program-visible output.

### M4 — flip the default path

`SelfHostedStrategy` becomes the default for `run`/`--lint`/`--eval`;
native pipeline remains available via an explicit flag for one release
cycle. `check_selfhost_fastpath.sh` stays green. The .hvc precompile path
(§3a of execution-path-map.md) is what makes this viable: user source is
compiled natively to .hvc ahead of/inline with launch, and the launcher
only executes bytecode.

### M5 — deletion

In spec order: `Lexer.cpp` (lexer.hv zero-diff), `Parser.cpp` + splits
(pratt zero-diff), `ByteCompiler.cpp` + splits (emitter parity), 
`SemanticAnalyzer.cpp` (types parity). Each deletion is its own commit with
the harness output attached.

## Blockers owned elsewhere (must land before/with M2)

1. **GC root hole** — this plan's lexer.hv repro is the regression test.
2. **Goroutine function-local write** (`docs/known-issues/goroutine-local-write.md`)
   — boxed upvalues (close-at-capture) is the agreed fix; it also changes
   the GC's open-cell marking path, so coordinate with the M2 GC work.
3. **CLOSURE fallback copies the ambient** (same doc, converging-defect #3)
   — module top-level functions get `module_globals = nullptr` and the
   fallback snapshots ambient globals; module state written into that copy
   is only traced via closures that hold it. Relevant to the .hv compiler's
   own module state once it runs under GC pressure.

## What stays C++ (permanent, per spec)

VM core (stack, dispatch, tiering), GC, fiber scheduler, evdev/X11/Wayland
backends, FFI, LLVM/Cranelift, host module system + stdlib modules, LSP.

## Rules

- All compiler fixes land in `modules/lang/*.hv`; the C++ frontend is
  bootstrap-only (read-only per spec) except for VM/GC correctness work,
  which is C++ by definition.
- Every milestone gate is a script in `scripts/ci/`, exit-code based, no
  narrative evidence.
- The sweep baseline (1481) may be pinned as a CI floor so coverage cannot
  silently regress while fixes proceed.
