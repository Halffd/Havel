# Two unrelated classes are both named DependencyTracker

**Status:** FIXED (2026-11-22). The incremental class was renamed to
`IncrementalDependencyTracker`, so `DependencyTracker` now names exactly one
thing. Recorded because it cost a full git-archaeology pass to answer a
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
## Resolved

`havel::compiler::incremental::DependencyTracker` was renamed to
`IncrementalDependencyTracker` across `DependencyGraph.hpp` (the class, its
`deserialize` return type, plus a doc comment recording why the name matters),
`DependencyGraph.cpp` (8 method definitions) and `IncrementalDriver.hpp`/`.cpp`
(3 + 1 references). No call site qualified the name with its namespace, so every
reference was the bare name and the substitution is name-only — no logic touched.

Correcting the estimate this file previously made, which understated the work:
`incremental::DependencyTracker` does **not** have "a single hit, the header's
own class definition". It has ~15 real references — 8 method definitions in
`DependencyGraph.cpp` plus 4 uses in `IncrementalDriver`. The qualified grep
returned one because no call site qualifies the name; that is a statement about
how it is *used*, not about how many references exist. Count the bare name, not
the qualified one.

The reactive tracker keeps its name: it has ~26 references across 12 files, it
is the more widely used of the two, and renaming the cheaper one is what
actually disambiguates a grep.
