# The first print() line carries a leading ANSI reset, breaking `^`-anchored parsing

**Status:** OPEN (cosmetic, exit-code-neutral). Pre-existing on main and
havel-2. **Retraction note:** an earlier version of this entry claimed the
first print was *lost*. That was wrong - it was a grep artifact: the print is
present, but the line begins with the colour reset `ESC[0m` bled from the
preceding logger line, so `grep '^XONE'` (and any line-anchored parser)
misses it while the clean second line matches. Cost an hour of phantom
diagnosis in the Oct 31 config investigation.

## Symptom

    printf 'print("XONE")\nprint("XTWO")\nexit(0)\n' > pr1.hv
    HAVEL_HEADLESS=1 ./build-debug/havel pr1.hv > out.txt
    sed -n '23,25p' out.txt | cat -A
    # ^[[0m^[[32m2026-... [INFO] havel_loader_load_module: ffi already loaded...$
    # ^[[0mXONE$
    # XTWO$

The reset belongs to the end of the logger line, but is emitted at the start
of the next write - which is the script's first print.

## Impact

- Any tooling or test that parses havel stdout with line anchors
  (`^word`) silently drops the first script-output line.
- Piping through `grep`/`awk` for probe output produces misleading "the
  statement never ran" conclusions (the Oct 31 incident).
- `--headless` output capture is affected too (the sequence is emitted
  regardless of terminal detection in these runs).

## Next step

Emit the colour reset at the END of each logger line, or strip/guard it when
stdout is not a TTY (or under --headless), and add a test asserting that the
first script output line starts with the printed text, not an escape.
