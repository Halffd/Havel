#!/usr/bin/env bash
# Architectural invariant: libhavel_lang (runtime + compiler) and
# libhavel_core must contain ZERO Qt symbols, and the minimal embedding
# executable (linked against exactly those archives, deliberately without
# Qt) must not pull any libQt at runtime. If someone re-introduces
# QClipboard/QWidget/etc. into the language or core archives, this fails
# in CI instead of forcing every embedding target to become a Qt
# application. Usage: embedding-qt-audit.sh [builddir]
set -u
DIR="${1:-build-nollvm}"
fail=0

# 1. The archives must be free of Qt-mangled symbols (string types count:
#    QString/QByteArray in a signature means a Qt header leaked in).
for a in "$DIR"/libhavel_runtime.a "$DIR"/libhavel_compiler.a \
         "$DIR"/libhavel_lang_core.a "$DIR"/libhavel_core.a; do
  [ -f "$a" ] || continue
  hits=$(nm -C -A "$a" 2>/dev/null | grep -cE 'Q(Clipboard|Widget|MainWindow|String|ByteArray|Application|Object|Pixmap|Window|Label|Dialog|Event|Point|Size|Rect|Json|File|Settings|Timer|Layout|Icon)[A-Za-z]*::|N2Qt')
  if [ "$hits" -ne 0 ]; then
    echo "AUDIT FAIL: $a carries $hits Qt-mangled symbol(s):"
    nm -C -A "$a" 2>/dev/null | grep -E 'Q(Clipboard|Widget|MainWindow|String|ByteArray|Application|Object|Pixmap|Window|Label|Dialog|Event|Point|Size|Rect|Json|File|Settings|Timer|Layout|Icon)[A-Za-z]*::|N2Qt' | head -8
    fail=1
  else
    echo "OK: $(basename "$a") Qt-free"
  fi
done

# 2. The minimal embedding executable must not link any Qt library.
EMB="$DIR/embedding_minimal"
if [ -x "$EMB" ]; then
  qtlibs=$(ldd "$EMB" 2>/dev/null | grep -i "libQt" || true)
  if [ -n "$qtlibs" ]; then
    echo "AUDIT FAIL: embedding_minimal links Qt:"
    echo "$qtlibs"
    fail=1
  else
    echo "OK: embedding_minimal links no Qt"
  fi
else
  echo "AUDIT FAIL: $EMB not found (build the embedding_minimal target)"
  fail=1
fi

if [ "$fail" -eq 0 ]; then
  echo "embedding-qt-audit: OK"
fi
exit $fail
