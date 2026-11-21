# Two unrelated classes are both named DependencyTracker

**Status:** OPEN (documentation hazard, not a code defect). No behavioral
bug. Recorded because it cost a full git-archaeology pass to answer a
question — "why was DependencyTracker deleted?" — whose answer was "it
wasn't".

## The question that cost the time

An audit question was posed: "find why `DependencyTracker` got deleted."
`grep -rn DependencyTracker src/` returns hits in ten files, and a deletion
commit is easy to find, so the naive read is "the reactive dependency
tracker was removed at some point and the docs are stale."

## The finding

`DependencyTracker` was **never deleted**. There are two unrelated classes
sharing the name in different namespaces:

| symbol | file | namespace | purpose |
|---|---|---|---|
| `DependencyTracker` | `src/havel-lang/runtime/concurrency/DependencyTracker.hpp:25` | `havel::compiler` | Reactive: records which globals/fields a watcher condition touched |
| `DependencyTracker` | `src/havel-lang/compiler/incremental/DependencyGraph.hpp:95` | `havel::compiler::incremental` | Compile-time: which modules/functions were compiled and their input fingerprints |

The reactive one is fully live at HEAD. It is instantiated at
`src/core/init/Havel.cpp:354, 403` and
`src/havel-lang/runtime/HavelEngine.hpp:431, 574`, and it is fed from twelve
`trackGlobalAccess`/`trackFieldAccess` call sites in
`src/havel-lang/compiler/vm/VMDispatch.cpp` (7) and
`src/havel-lang/compiler/vm/VMCollections.cpp` (3).

## What the deletion commit actually did

`da65314d0` (2026-08-06) — "unify reactive watcher dispatch to sync path
only" — removed 8 uses of the tracker from the **async** VAR_CHANGED handler
and added 4 uses to the **sync** path. Net reduction from consolidation of
two code paths into one, not a removal of the feature.

Why it matters: the failing theme the commit fixed was that async watcher
re-firing flooded the event queue and stalled the REPL during
array-length reactive loads. The tracker was rebuilt on the sync path to
keep dependency recording working after the dispatch unified. So the
instrumentation and the dispatch are both present; only the *site count*
dropped.

## Why this is worth a known-issue entry

Not a bug. A hazard for anyone auditing the subsystem:

- A grep for the class name mixes two unrelated systems, so a reader cannot
  tell from hits alone whether reactive tracking is wired.
- A `git log -S'DependencyTracker'` shows a real -8/+4 hunk, which reads
  like the feature being gutted when it was consolidated.
- The docs that mention "DependencyTracker"
  (`docs/specs/conditional-hotkeys.md`,
  `docs/known-issues/conditional-hotkey-window-reactivity.md`) refer to the
  reactive one, without saying so.

## Recommended next step

Rename one of the two so grep is unambiguous. The incremental one is the
better rename candidate (it is newer, less referenced, and `DependencyGraph`
already sits in the same file), e.g. `IncrementalDependencyTracker`.
Measured cost: `grep -rn 'incremental::DependencyTracker' src/` returns a
single hit (the header's own class definition) — no call site qualifies the
name with its namespace, so every external reference is to the bare name and
the rename is a substitution inside ~3 files
(`DependencyGraph.hpp`, `IncrementalDriver.hpp/.cpp`). Behavior-neutral, and
it removes the audit trap permanently. Not done here: this change was a
diagnosis task, and a rename belongs with its own verification.