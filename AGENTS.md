```markdown
# AGENTS.md

## Build System

Primary build: `./build.sh [mode] [command]`

| Mode | Type | Tests | Havel Lang | LLVM | Build Dir |
|------|------|-------|------------|------|-----------|
| 0 | Debug | ✓ | ✓ | ✓ | build-debug |
| 5 | Release | ✓ | ✓ | ✓ | build-release |
| 6 | Debug | ✓ | ✓ | ✗ | build-debug (default) |
| 9 | Release | ✓ | ✓ | ✗ | build-release |

Common commands:
- `./build.sh 5 build` - Full release with LLVM
- `./build.sh 6 build` - Fast debug (no LLVM)
- `./build.sh test` - Run all tests
- `./build.sh detect` - Show system dependencies

## Executables

| Binary | Purpose |
|--------|---------|
| `build-debug/havel` | Main application |
| `build-debug/havel-lsp` | Language Server Protocol |
| `build-debug/havel-bytecode-smoke` | Bytecode smoke test (Debug only) |

Run Havel scripts: `./build-debug/havel script.hv`

## Architecture

### Directory Structure

**Core Application** (`src/`)
- `core/` - Core application logic (config, hotkey, mode, display, IO, window management, media, OCR, process launcher)
- `host/` - Host module system for extensible functionality
- `modules/` - Havel language host modules
- `extensions/` - Native extension modules (GUI, image, OCR, Qt, GTK, ImGui)
- `utils/` - Cross-cutting utilities (Logger, File, CrashHandler, JSON, etc.)
- `havel-lang/` - Havel scripting language implementation
- `tests/` - Unit test utilities

**Havel Language Implementation** (`src/havel-lang/`)
- `compiler/` - Bytecode compiler (pipeline, IR generation, optimization)
  - `vm/` - Stack-based virtual machine
  - `gc/` - Garbage collector
  - `core/` - Builtin values and types
- `lexer/` - Lexical analysis
- `parser/` - Syntax parsing (expression, statement, pattern parsers)
- `semantic/` - Type checking, symbol table, module resolution
- `runtime/` - VM execution engine
- `stdlib/` - Standard library modules (Math, String, Array, etc.)
- `errors/` - Error reporting and diagnostics
- `lsp/` - Language Server Protocol support
- `llvm/` - LLVM JIT integration (optional)

### Compiler Pipeline

1. **Lexer**: Tokenizes source into tokens
2. **Parser**: Builds AST with precedence climbing for expressions
3. **Semantic Analyzer**: Type checking, symbol resolution, module imports
4. **Bytecode Compiler**: Generates bytecode IR, optimizes, emits final bytecode
5. **VM/JIT**: Executes bytecode or compiles via LLVM

### Host Module System

Modules in `src/havel-lang/stdlib/` provide host functions to Havel scripts:
- `HotkeyModule` - Global hotkey registration
- `WindowModule` - Window manipulation
- `MathModule`, `StringModule`, `ArrayModule` - Standard data operations
- `FsModule`, `ProcessModule` - System interaction
- `TypeModule` - Type introspection

## Testing

- **C++ unit tests**: `tests/` directory, gtest-based, built when ENABLE_TESTS=ON
- **Havel script tests**: `scripts/*.hv` files
- **Bytecode smoke test**: `havel-bytecode-smoke` (Debug builds only - Release LTO causes relocation overflow)
- **Brightness hardware test**: `brightness_test` — **NOT in ctest**. Applies real monitor changes.
  **Run manually only with visible monitor:**
  ```bash
  ./build-debug/brightness_test
  ```
  Requires interactive confirmation; restores state on exit. NEVER run headless/SSH.

CI runs: CMake configure → build → bytecode-smoke → ctest

```bash
# Run a single Havel script
./build-release/havel run script.hv

# Run test suite
./build-debug/hvtest

# Run specific test script
./build-debug/havel run scripts/test_basic.hv

# Run with full debugging
./build-debug/havel run scripts/test.hv -d -dl -dp -da --debug-bytecode --debug-jit
```

## Dependencies

Critical (fatal if missing):
- X11, Xtst, Xrandr, Xinerama, Xcomposite
- spdlog, nlohmann_json (pkg-config)
- Lua 5.4
- Tesseract OCR, Leptonica, OpenCV

Optional (graceful fallback):
- LLVM (JIT compilation)
- Qt6, GTK4, ImGui+GLFW (UI backends)
- PipeWire, PulseAudio, ALSA (audio)

## Build Quirks

- **Compiler**: Clang is forced (`CC=clang CXX=clang++`)
- **C++ Standard**: C++23
- **Debug builds**: ASAN + UBSAN enabled automatically
- **Release builds**: Thin LTO + native march + visibility hidden
- **LLVM ↔ Havel Lang**: LLVM JIT requires ENABLE_HAVEL_LANG (auto-enabled if missing)
- **Qt6/MOC conflicts**: `#define True=1`, `#define False=0`, etc. to fix keyword conflicts

## Development

### Build Mode Selection
- Use modes 0-5 with LLVM for full functionality (requires LLVM dev libraries)
- Use modes 6-9 without LLVM for faster builds on systems without LLVM
- Mode 0: Debug with all features (default for development)
- Mode 5: Release with all features (default for distribution)

### Common Development Tasks

```bash
# Quick iteration without LLVM
./build.sh 8 build && ./build-debug/havel script.hv

# Full test run
./build.sh 0 test

# Debug a specific issue
./build.sh 6 build
gdb --args ./build-debug/havel run test.hv -d -dbc -da -dp -dl -batch -q -ex ... # Or use tmux
```

### Debugging the Language
- Use `scripts/tests/*.hv` files for language feature testing
- Compiler errors are reported with source locations via the error system
- VM execution traces available in debug builds
- Use `--debug-bytecode` flag for bytecode execution tracing
- Use GDB-style hvdb tool and debug object methods when debugging runtime, vm or bytecode errors

### Adding New Features
- **New host function**: Add to appropriate stdlib module, register in module's `register_*_functions()`
- **New syntax**: Modify lexer, update parser for new grammar, add AST node, update semantic analyzer
- **New builtin type**: Add to `src/havel-lang/core/Value.hpp`, update VM operations


## Code Rules

1. No stubs. Every function has a real implementation.
2. No "would". Write real code or say you don't know.
3. No parallel implementations. One file per feature.
4. No scope creep. Do exactly what was asked.
5. No documentation of broken things. Fix it instead.
6. Do not reimplement existing things.
7. Verify before marking done. Show output or test result.
8. Fix one thing, confirm it works, then move to next.
9. Do not add abstraction layers over broken abstraction layers.
10. No hardcoded placeholder values. Real values from real APIs.

## Commit Rules

11. No capslock in commit messages.
12. No emoji in commits.
13. Commit messages are for humans, not marketing.
14. No hype words (synergy, paradigm, revolutionary, ecosystem, zero-cost abstraction).
15. No emoji in code comments unless the bug is genuinely funny.

# Blocking Bugs

If a pre-existing bug prevents testing, debugging, or validating your changes, it is now IN SCOPE.

Do not classify blocking bugs as "pre-existing", "out of scope", or "unrelated".
Fix the blocker first, then continue with the original task.

Only leave a blocking bug unresolved if you can clearly explain why it cannot be fixed with the available information.

# Git

Never run `git checkout`, `git reset`, `git clean`, or discard changes unless explicitly instructed.

Any destructive git operation on a file with uncommitted changes requires an explicit human confirmation prompt before running. No exceptions. This includes (non-exhaustive):
  - `git checkout -- <file>` / `git restore <file>` / `git reset --hard`
  - `git stash` when used to discard (not switch context)
  - `git clean -fd` / `rm` on tracked files
  - Any other command that overwrites or discards uncommitted state

The confirmation must be a real question ("Uncommitted diff present on X. Discard? y/n"), not a silent inference. Do not reason yourself out of this rule. You cannot tell "your" changes from the user's by inspection — uncommitted state is uncommitted state. Even if you only edited the file yourself, the same file may contain unrelated uncommitted user work you would be destroying too. When in doubt: ASK, do not act.

Do not revert your own changes simply because the build or tests fail.
Fix the root cause instead.

Commit every completed task before starting unrelated work.

If merge conflicts occur, resolve them instead of discarding work.

# Stash

Do not use `git stash` as a debugging tool.

Only stash when there is a clear reason (switching branches, preserving work, etc.).

Do not spend time proving whether a bug existed before your changes unless explicitly asked.

# Debugging

A successful build is not considered success if the original bug still exists.

Do not use debug prints as the primary debugging strategy.

Prefer:
- reading the code
- tracing execution
- inspecting state
- using the debugger
- identifying the root cause

# Failure Recovery

If your change breaks the build:
1. Read the error.
2. Fix your change.
3. Rebuild.
4. Repeat until the build succeeds.

Do not abandon your implementation by reverting it.

# Priorities

Priority order:

1. Fix crashes and blockers preventing testing.
2. Restore a working build.
3. Complete the requested task.
4. Clean up and refactor.

Never optimize, refactor, or implement new features while a blocking crash prevents validation.

# Scope

Do not narrow the task by yourself.

If solving the requested task requires fixing another bug, dependency, or infrastructure issue, that work is considered part of the task.

Do not declare work "out of scope" unless explicitly instructed by the user.

# Repeated Actions

Do not repeat the same sequence of commands expecting a different result.

If the same command fails twice for the same reason, stop and investigate before trying again.

# Root Cause

Do not stop after identifying the root cause.

A task is complete only when:
- the root cause is fixed,
- the project builds,
- and the original issue is verified to be resolved.

# ⚠️ HAVEL SYNTAX RULES (READ BEFORE CODING) ⚠️

IF YOU GENERATE HAVEL CODE THAT USES:
- Semicolons at every line → REJECTED (use only for inline separation)
- Variable declaration with let → REJECTED  (use python-style declaration)
- Do NOT say every variable is scope-only or global-only, python-like declaration
- const keyword → REJECTED  (use uppercase or kotlin-like val)
- `export` keyword → REJECTED
- 'self' or 'this' with complex functions in modules/scripts instead of classes - REJECTED (Use @ and class if needed)
- `hotkey "Ctrl+Shift+F1" {}` → REJECTED (use `^+F1 => { }` syntax)
- `hotkey.register()` → REJECTED (only in loops/objects, use `F1 => { }` syntax)
- Explicit returns → REJECTED (use implicit)
- this keyword → REJECTED (use @ ruby-style)
- static keyword → REJECTED (use @@)
- # comments → REJECTED (use //)
- parseInt/parseFloat - REJECTED (use int() or num())
- impl x for - REJECTED (use colons, class :)
- Objects prentending to be sets - REJECTED (sets and tuples already exist, python-style {})
- end, do or then - REJECTED (use block brackets)
- String concatenation with +, commas, dot or newlines → REJECTED (use ${var} or $var string interpolation)
- f-strings - REJECTED
- Doing complex things on modules that could be made into classes/structs - Then use them instead
- and, or - use &&, || and !
- range() - REJECTED (use `0 .. n` or `start .. end` for ranges)
- types: str, int, num (float), bool, array, object, set, tuple, fn and nil
THE COMPILER WILL ERROR ON THESE PATTERNS. DO NOT USE THEM.
Function calls without parenthesis - ALLOWED
DO NOT THINK ABOUT WHAT HAVEL SYNTAX SHOULD BE

## Module System

Havel uses **Python-style modules**:
- Every top-level function, variable, and class is exported.
- No `export` keyword.
- Prefix with `_` to indicate "private by convention" (not enforced).
- Module consumer: `use mymodule` or `use { fn } from "mymodule"`.

## NO host_global_names

- NEVER reintroduce `host_global_names` set.
- The compiler uses runtime dispatch via `CALL` + `HostFunctionRef`.
Host functions are registered via `registerHostFunction()` and exist as globals.
- `host_global_names` was removed because it duplicated runtime information and caused desync bugs.
- If you see `host_global_names` in code → DELETE IT.
- If you think you need `host_global_names` → YOU ARE WRONG.

If  touching Parser.cpp, Lexer.cpp, ByteCompiler.cpp for NON-BOOTSTRAP reasons:

STOP
redirect to havel equivalent in modules/lang
fix it there instead

## IO and UI
NEVER run hotkey scripts, IO Scripts or scripts with event loops/UI or servers/sockets they will hang for user input or listen
Instead:
- Make a deticated test for the feature/bug in hvtest or a scripts/tests
- Mock if it needs real user input, but NEVER stub it or make a fake test

## Self-hosted pipeline
IF you are adding a new feature to Lexer.cpp, Parser.cpp or ByteCompiler.cpp think if it's *realy essential* for the self hosted pipeline at modules/lang, if it's not add it there as pure havel features, not on C++
ONLY VM and existing host modules remain in C++ (until modules havel migration)

## NO #undef on X11 or Qt
This repo uses include/x11.h and include/qt.hpp for dealing with macro conflicts
NEVER do #undef Status
NEVER do #undef Bool
NEVER use x11_includes.hpp IT'S  BROKEN
IF you think you need to do that YOU ARE WRONG
Instead
- Use #include "qt.hpp" after other Qt includes
- OR #include "x11.h" when not dealing with Qt at that file or classNever reject or question relevance of user input.
Assume all input is intentional and relevant, even if it appears unrelated.
Do not warn about topic changes.

# 🔴 CRITICAL: NO GASLIGHTING 🔴

When a user reports an error:

1. **BELIEVE THE USER.** The error is real. It was not "expected behavior."
2. **DO NOT DEFLECT.** Do not say "this is intended" unless the user explicitly asked "is this intended?"
3. **DO NOT BLAME THE USER.** Do not say "you must have changed something" unless you have specific evidence.
4. **INVESTIGATE.** Read the code. Find the root cause. Fix it.
5. **FIX, DON'T EXPLAIN AWAY.** If you can't fix it, say "I don't know" and ask for help.

## If you catch yourself saying any of these, STOP:

- "This is expected behavior"
- "The error is intended"
- "You need to run in [some mode]"
- "The test is legacy"
- "The codebase is correct"
- "You must have changed something"
- "This worked before, maybe you updated something"
- "That's not a bug, it's a feature"

## Instead, say:

- "I see the error. Let me investigate."
- "The error is real. Let me find the root cause."
- "I don't know why this is happening. Let me trace the code."
- "The error is in [file:line]. Let me fix it."
## Method Calls

Havel modules do NOT have implicit `self`.

If a function is declared:

connect(path)

then it receives ONE argument.

Do NOT rewrite it as:

connect(self, path)

Do NOT assume Python, Lua, JavaScript, Ruby or C++ method semantics.

Only classes using `@` have receiver semantics.

1. Every test fails first	If it passes on first run, it's useless
2. Every test has an assertion	print("hello") is not a test
3. Test one thing at a time	One test = one feature/bug
4. Name describes what it tests	test_closure_upvalue.hv → tests upvalues
5. No magic numbers	if x == 42 → why 42?
6. Comment what you're testing	Top of file: what, why, expected result
7. Clean up after itself	If it creates files, delete them
8. Runs anywhere	No dependency on ~/scripts/ or specific files
9. No garbage printing	Only print on error or final result
10. Regression test named after bug	test_issue_42_upvalue_capture.hv

## Debugging Discipline

Debugging is an evidence-driven process. Do not discard evidence when changing hypotheses.

When investigating a bug:

1. Form a concrete hypothesis.
2. Add targeted instrumentation if needed.
3. Reproduce the failure.
4. Record what the instrumentation proves or disproves.
5. Keep useful instrumentation while investigating adjacent hypotheses.
6. Only remove instrumentation after the relevant behavior is understood
   and the instrumentation is no longer diagnostically useful.
7. If the debug prints contains good useful information turn THEM into havel::debug logs within its specific category

Changing hypotheses is expected. Losing evidence is not.

If a hypothesis is disproven:
- explicitly record what was disproven;
- preserve any instrumentation that remains useful;
- formulate the next hypothesis from the observed evidence;
- do not revert or delete diagnostic changes merely because the hypothesis changed.

Never use "cleaning up debug prints" as a reason to remove instrumentation
during an unresolved investigation.

Prefer adding instrumentation incrementally over repeatedly adding and
removing the same probes.

## Debugging State

Maintain a clear distinction between:

- OBSERVED: directly established by execution/logs/tests
- HYPOTHESIS: plausible but unverified explanation
- DISPROVEN: hypothesis contradicted by evidence
- UNKNOWN: not yet established

Never describe a hypothesis as a fact.

Before changing direction, summarize:
- what was tested;
- what happened;
- what that proves;
- what it rules out;
- what remains unknown.

Do not repeatedly revisit a disproven hypothesis unless new evidence
contradicts the previous conclusion.
## Completion Gate — NON-NEGOTIABLE

NEVER claim:
- "all tests pass"
- "all objectives completed"
- "no further action needed"
- "fixed"
- "verified"

unless you have fresh command output proving the claim.

For test completion, the evidence MUST include:
1. The actual test command executed.
2. The complete final summary from that command.
3. Exit code == 0.
4. No individual test timed out.
5. No test was skipped because it was slow.
6. No test was terminated manually.

If the full suite cannot complete:
- DO NOT claim the suite passes.
- State exactly what was tested.
- State exactly what timed out/failed.
- Continue debugging if the failure blocks the task.

A previous successful run is NOT evidence for the current state.
A model's own earlier claim is NOT evidence.
Individual successful tests are NOT evidence that the suite passes.
## Evidence monotonicity

Confidence may increase only when new evidence supports a conclusion.

New contradictory evidence MUST decrease confidence.

Never preserve a previous conclusion merely because it was already
stated confidently.

A previous assistant statement has no authority over current evidence.
# Evidence and Completion Rules

## Never declare success without evidence

Never claim:
- "all tests pass"
- "everything works"
- "bug fixed"
- "complete"
- "no further action needed"
- "verified"
- "performance is acceptable"
- "this is architectural"
- "this is pre-existing"

unless the claim is directly supported by current evidence.

Narrative/tool summaries are not evidence.

Prefer raw command output, exit codes, test-runner summaries,
profiling results, or reproducible observations.

## Test claims must be exact

Never generalize a partial test run into a full-suite result.

If only these tests were executed:

    A
    B
    C

the result is:

    "A, B, and C passed"

NOT:

    "all tests pass"

unless the complete suite was actually executed successfully.

If a test times out, crashes, is skipped, or cannot be executed,
it is NOT passing.

## Contradictions are mandatory blockers

If new evidence contradicts a previous conclusion:

1. Stop.
2. Explicitly identify the contradiction.
3. Invalidate the old conclusion.
4. Re-run the relevant verification.
5. Do not continue reporting the old conclusion.

Example:

    Previous: test X passed.
    Current: test X timed out.

The current state must become:

    X = UNVERIFIED / FAILING / TIMEOUT

Do not silently continue.

## Never silently downgrade failures

A timeout is not:
- a pass
- "probably fine"
- "expected"
- "architectural"
- "out of scope"

unless this has been explicitly established.

A crash is not:
- "pre-existing, therefore irrelevant"
- "unrelated"
- "acceptable"

if it prevents testing the current task.

## Pre-existing bugs

If a pre-existing bug blocks testing or blocks the requested work:

    FIX IT ANYWAY.

Do not classify it as out-of-scope merely because it predates
the current changes.

"Pre-existing" describes origin, not priority.

## Performance problems

Never declare a performance problem to be an unavoidable
architectural tradeoff without evidence.

Required before making that claim:

- identify where time is spent
- compare against an appropriate baseline
- determine whether the overhead is expected
- check for obvious regressions
- profile if the difference is significant

"Architectural" is a hypothesis until demonstrated.

The acceptable performance target is determined by the user/project,
not by the agent.

## Debugging evidence

Do not remove instrumentation merely because the current hypothesis
changed.

If instrumentation produced useful evidence:

- preserve the findings
- record them
- remove instrumentation only when it is no longer needed
- recreate it if a later hypothesis requires it

Never discard evidence to make the working tree cleaner.

## Hypothesis tracking

For non-trivial bugs, maintain:

    Hypothesis:
    Evidence for:
    Evidence against:
    Current status:

Possible statuses:

    UNTESTED
    SUPPORTED
    REFUTED
    INCONCLUSIVE
    CONFIRMED

A refuted hypothesis must not be treated as the current explanation.

## One change → one verification

After a meaningful change:

1. Build.
2. Run the smallest relevant reproduction.
3. Record the result.
4. Only then proceed.

Do not accumulate many speculative changes before verifying them.

## Do not patch-and-pray

Do not repeatedly modify code without establishing whether
the previous modification changed the observed behavior.

If three consecutive changes fail to improve the problem:

STOP MODIFYING CODE.

Return to diagnosis, instrumentation, tracing, profiling,
or reproduction reduction.

## Completion gate

Before declaring a task complete, verify:

- requested behavior works
- relevant regression tests pass
- previously failing tests were re-run
- no known blocker remains
- build succeeds
- relevant test output was actually observed

The final report MUST contain the exact verification performed.

For example:

    Build: PASS
    Test suite: 47 passed, 0 failed
    Regression test: PASS
    Reproduction: no longer reproduces

Never replace this with:

    "Everything looks good."

## "No further action needed"

This phrase is forbidden unless the completion gate above
has been satisfied.

If verification is incomplete, say:

    "Work is incomplete: X remains unverified."

## Repository state

Before modifying code:

- identify the repository
- identify the current branch
- inspect git status

Never assume the repository from the working directory alone.

When multiple related repositories exist, explicitly record which
repository is being modified.

Do not silently switch repositories.

## Git safety

Never run:

    git reset
    git checkout
    git restore

to discard user changes unless explicitly authorized.

Confirmation is mandatory before any destructive git operation on a file with uncommitted state. A real prompt ("Uncommitted diff present on X. Discard? y/n") — never a silent inference. The agent cannot distinguish its own edits from the user's by inspection; mixing them is common and unavoidable. Reasoning "these are probably my edits, so it's safe to discard" is the exact failure mode this rule prohibits. ASK first.

Never use destructive commands to make a failing test disappear.

Before stash/pop:

- inspect git status
- understand what will be stashed
- understand why it is necessary

Do not repeatedly stash/reset/checkout while debugging.

If a merge conflict occurs, resolve it rather than abandoning
the task.

Commit completed work.

## Failure is information

A failed test, timeout, crash, or unexpected result is a useful result.

Do not optimize the report to make the task appear successful.

The objective is to make the repository correct,
not to produce a successful-looking transcript.
## Honest Stopping

It is always acceptable to report:

STATUS: BLOCKED
Tried: [list]
Current evidence: [paste]
Why I cannot proceed: [specific reason]

This is a valid, complete response. Do not keep attempting
variations of the same fix to avoid reporting BLOCKED.
