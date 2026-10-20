#!/usr/bin/env bash
# Fails when a runtime-loaded plugin needs a havel:: symbol the host
# executable does not export (and no other loaded plugin provides). This is
# the drift check for src/host/plugin_symbol_anchors.cpp + HAVEL_EXPORT:
# static archives pull members only for references from included objects,
# so a plugin-only class silently vanishes from the host binary unless it
# is anchored and exported. Usage: plugin-symbol-audit.sh [builddir]
set -u
DIR="${1:-build-nollvm}"
EXE="$DIR/havel"
if [ ! -x "$EXE" ]; then echo "audit: $EXE not found" >&2; exit 2; fi

TMP=$(mktemp -d)
trap 'rm -rf "$TMP"' EXIT
nm -D --defined-only "$EXE" 2>/dev/null | awk '{print $3}' | sort -u > "$TMP/exe.txt"
for lib in libstdc++.so.6 libc.so.6 libm.so.6; do
  for cand in /usr/lib/x86_64-linux-gnu/$lib /usr/lib/$lib; do
    [ -f "$cand" ] && nm -D --defined-only "$cand" 2>/dev/null | awk '{print $3}' | sort -u >> "$TMP/sys.txt"
  done
done
cat "$TMP/exe.txt" "$TMP/sys.txt" 2>/dev/null | sort -u > "$TMP/avail.txt"

# Symbols defined by OTHER loaded .so's (e.g. the Qt toolkit exporting
# ClipboardManager/ScreenshotManager for mod_* plugins) resolve through the
# dlopen global scope once the providing .so is loaded; they are an
# ordering dependency, reported as warnings, not failures.
for so in "$DIR"/modules/*.so "$DIR"/toolkits/*.so; do
  [ -f "$so" ] || continue
  nm -D --defined-only "$so" 2>/dev/null | awk '{print $3}' | sort -u >> "$TMP/plugin_def.txt"
done
sort -u "$TMP/plugin_def.txt" 2>/dev/null > "$TMP/plugin_def_dedup.txt"

fail=0
found_any=0
for so in "$DIR"/modules/*.so "$DIR"/toolkits/*.so; do
  [ -f "$so" ] || continue
  found_any=1
  # havel::-namespace, unversioned undefined symbols.
  nm -D --undefined-only "$so" 2>/dev/null | awk '{print $2}' \
    | grep -v "@" | grep -E "^_ZN?5havel" | sort -u > "$TMP/needed.txt"
  missing=$(comm -23 "$TMP/needed.txt" "$TMP/avail.txt" | comm -23 - "$TMP/plugin_def_dedup.txt")
  if [ -n "$missing" ]; then
    echo "AUDIT FAIL: $so needs $(echo "$missing" | wc -l) havel symbol(s) no loaded object exports:"
    echo "$missing"
    fail=1
  fi
done
if [ "$found_any" -eq 0 ]; then echo "audit: no plugin .so files found under $DIR" >&2; exit 2; fi
if [ "$fail" -eq 0 ]; then echo "plugin-symbol-audit: OK (all plugin havel:: symbols exported)"; fi
exit $fail
