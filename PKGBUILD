# Maintainer: Half <guhalfeld@hotmail.com>
pkgname=havel
pkgver=0.1.0
pkgrel=1
pkgdesc="Havel window manager and scripting language"
arch=('x86_64')
url="https://github.com/Halffd/Havel"
license=('custom')
depends=('qt6-base' 'lua' 'mpv')
makedepends=('cmake' 'make' 'gcc' 'pkg-config')
options=('!strip')
source=("${pkgname}-${pkgver}.tar.gz::https://github.com/Halffd/Havel/archive/refs/tags/v${pkgver}.tar.gz")
sha256sums=('SKIP')

build() {
    cd "${srcdir}/Havel-${pkgver}"
    cmake -B build \
        -DCMAKE_BUILD_TYPE=Release \
        -DCMAKE_INSTALL_PREFIX=/usr
    cmake --build build -j"$(nproc)"
}

package() {
    cd "${srcdir}/Havel-${pkgver}"

    # Install binary
    install -Dm 755 build/havel "${pkgdir}/usr/bin/havel"

    # Install library
    if [ -f build/libhavel_lang.a ]; then
        install -Dm 644 build/libhavel_lang.a "${pkgdir}/usr/lib/libhavel_lang.a"
    fi

    # Install pkg-config file
    if [ -f havel.pc ]; then
        sed "s|@PREFIX@|/usr|g" havel.pc > "${pkgdir}/usr/lib/pkgconfig/havel.pc"
    fi

    # Install headers
    for dir in src/havel-lang/ast src/havel-lang/compiler src/havel-lang/core \
               src/havel-lang/lexer src/havel-lang/parser src/havel-lang/runtime \
               src/havel-lang/stdlib src/havel-lang/utils src/havel-lang/common \
               src/havel-lang/errors src/havel-lang/types src/havel-lang/tools \
               src/havel-lang/syntax src/havel-lang/semantic; do
        if [ -d "$dir" ]; then
            local subdir="usr/include/havel/$(basename "$dir")"
            mkdir -p "${pkgdir}/${subdir}"
            find "$dir" -maxdepth 1 -name '*.h' -o -name '*.hpp' | while read -r f; do
                install -m 644 "$f" "${pkgdir}/${subdir}/"
            done
        fi
    done

    # Install license
    if [ -f LICENSE ]; then
        install -Dm 644 LICENSE "${pkgdir}/usr/share/licenses/${pkgname}/LICENSE"
    fi
}
