# The first print() of a script produces no output (stdout buffer lost at startup)

**Status:** OPEN. Pre-existing on main (verified Oct 31 on main's
`build-debug/havel` at 47081c896) and on havel-2 tip 649a3ee7f. Not caused by
the config-loader work or the unstaged VM WIP.

## Symptom

The first `print()` of every script is swallowed; the second and later prints
appear normally. Exit codes and assertions are unaffected, so smoke verdicts
stay correct - but any probe whose first diagnostic line is its first print
reads as "the statement never ran", which is how this was found (an hour of
runtime-diagnosis probes pointed at phantom config-object behavior because
probe line 1 kept vanishing).

Reproducer:

    printf 'print("XONE")\nprint("XTWO")\nexit(0)\n' > pr1.hv
    HAVEL_HEADLESS=1 ./build-debug/havel pr1.hv
    # prints XTWO only; XONE is gone

Same on main's binary: only XTWO prints.

## Leading hypothesis (UNVERIFIED)

A stdout re-open/redirect (logging setup, terminal/UI init, or the headless
guard) runs after the VM has already buffered the first print; the buffered
bytes are discarded with the old stream. Consistent with: exactly one leading
print lost, later prints unaffected, exit-code-neutral, present on both
trees.

## Next step

Find the stdout (re)configuration that runs once at startup - grep for
`freopen`, `dup2`, `setvbuf`, `std::cout` rebinds, and logger initialization in
`src/core/init/HavelLauncher.cpp` and the Logger setup - and either flush
before rebinding or initialize before the first script bytecode runs. Add a
two-print regression check to the smoke suite once located.
