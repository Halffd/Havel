#!/usr/bin/env bash
# Install script for Havel window manager / language
# Usage: ./install.sh [--prefix /usr/local]
set -euo pipefail

PREFIX="${PREFIX:-/usr/local}"
BINDIR="$PREFIX/bin"
LIBDIR="$PREFIX/lib"
INCLUDEDIR="$PREFIX/include/havel"
SHAREDIR="$PREFIX/share/havel"
PKGLIBDIR="$PREFIX/lib/pkgconfig"

echo "Installing Havel to $PREFIX"

# Create directories
mkdir -p "$BINDIR" "$LIBDIR" "$INCLUDEDIR" "$SHAREDIR" "$PKGLIBDIR"

# Install binary
if [ -f build-debug/havel ]; then
    install -m 755 build-debug/havel "$BINDIR/havel"
    echo "  Installed: $BINDIR/havel"
elif [ -f build/havel ]; then
    install -m 755 build/havel "$BINDIR/havel"
    echo "  Installed: $BINDIR/havel"
else
    echo "Error: No havel binary found in build-debug/ or build/"
    echo "Run 'cmake --build build-debug' first."
    exit 1
fi

# Install library
if [ -f build-debug/libhavel_lang.a ]; then
    install -m 644 build-debug/libhavel_lang.a "$LIBDIR/libhavel_lang.a"
    echo "  Installed: $LIBDIR/libhavel_lang.a"
fi

# Install pkg-config file
if [ -f havel.pc ]; then
    sed "s|@PREFIX@|$PREFIX|g" havel.pc > "$PKGLIBDIR/havel.pc"
    echo "  Installed: $PKGLIBDIR/havel.pc"
fi

# Install headers
for dir in src/havel-lang/ast src/havel-lang/compiler src/havel-lang/core \
           src/havel-lang/lexer src/havel-lang/parser src/havel-lang/runtime \
           src/havel-lang/stdlib src/havel-lang/utils src/havel-lang/common \
           src/havel-lang/errors src/havel-lang/types src/havel-lang/tools \
           src/havel-lang/syntax src/havel-lang/semantic; do
    if [ -d "$dir" ]; then
        subdir="$INCLUDEDIR/$(basename "$dir")"
        mkdir -p "$subdir"
        find "$dir" -maxdepth 1 -name '*.h' -o -name '*.hpp' | while read -r f; do
            install -m 644 "$f" "$subdir/"
        done
    fi
done
echo "  Installed: headers to $INCLUDEDIR"

# Install standard library scripts
if [ -d scripts ]; then
    mkdir -p "$SHAREDIR/scripts"
    cp -r scripts/* "$SHAREDIR/scripts/" 2>/dev/null || true
    echo "  Installed: scripts to $SHAREDIR/scripts"
fi

echo ""
echo "Havel installed successfully."
echo ""
echo "To use shebang scripts, add $BINDIR to your PATH:"
echo "  export PATH=$BINDIR:\$PATH"
echo ""
echo "Example shebang script:"
echo '  #!/usr/bin/env havel'
echo '  print("Hello from Havel!")'
echo ""
echo "Then: chmod +x script.hv && ./script.hv"
