# Maintainer: Half <guhalfeld@hotmail.com>
pkgname=havel
pkgver=0.1.0
pkgrel=1
pkgdesc="Havel window manager and scripting language"
arch=('x86_64')
url="https://github.com/Halffd/Havel"
license=('MIT')
depends=(
    'qt6-base'
    'qt6-charts'
    'lua54'
    'mpv'
    'libx11'
    'libxrandr'
    'libxinerama'
    'libxcomposite'
    'libxtst'
    'libxi'
    'libxfixes'
    'libxdamage'
    'libpulse'
    'alsa-lib'
    'dbus'
    'pcre2'
    'opencv'
    'tesseract'
    'leptonica'
    'spdlog'
    'nlohmann-json'
    'wayland'
    'pipewire'
)
makedepends=(
    'cmake'
    'ninja'
    'clang'
    'llvm'
    'pkgconf'
    'wayland-protocols'
)
optdepends=(
    'llvm: LLVM JIT compilation for Havel language'
    'libpipewire: PipeWire audio support'
    'libxdamage: Extended damage reporting'
    'readline: Enhanced REPL with history'
)
options=(
    '!strip'
    '!lto'
    'staticlibs'
)
provides=('havel-lang')
conflicts=()
source=("${pkgname}-${pkgver}.tar.gz::https://github.com/Halffd/Havel/archive/refs/tags/v${pkgver}.tar.gz")
sha256sums=('SKIP')

get_jobs() {
    local nproc
    if command -v nproc &>/dev/null; then
        nproc=$(nproc)
    elif [[ -f /proc/cpuinfo ]]; then
        nproc=$(grep -c ^processor /proc/cpuinfo)
    else
        nproc=4
    fi
    echo "$((nproc > 16 ? 16 : nproc))"
}

build() {
    cd "${srcdir}/Havel-${pkgver}"
    
    local jobs
    jobs=$(get_jobs)
    
    cmake -B build \
        -G Ninja \
        -DCMAKE_BUILD_TYPE=Release \
        -DCMAKE_INSTALL_PREFIX=/usr \
        -DCMAKE_C_COMPILER=clang \
        -DCMAKE_CXX_COMPILER=clang++ \
        -DENABLE_LLVM=ON \
        -DENABLE_TESTS=OFF \
        -DENABLE_HAVEL_LANG=ON \
        -DUSE_CLANG=ON \
        -Wno=dev
    
    ninja -C build -j"${jobs}"
}

check() {
    cd "${srcdir}/Havel-${pkgver}"
    
    local jobs
    jobs=$(get_jobs)
    
    cmake -B build-test \
        -G Ninja \
        -DCMAKE_BUILD_TYPE=Debug \
        -DCMAKE_INSTALL_PREFIX=/usr \
        -DCMAKE_C_COMPILER=clang \
        -DCMAKE_CXX_COMPILER=clang++ \
        -DENABLE_LLVM=ON \
        -DENABLE_TESTS=ON \
        -DENABLE_HAVEL_LANG=ON \
        -DUSE_CLANG=ON \
        -Wno=dev
    
    ninja -C build-test -j"${jobs}"
    ninja -C build-test test || true
}

package() {
    cd "${srcdir}/Havel-${pkgver}"
    
    local jobs
    jobs=$(get_jobs)
    
    ninja -C build -j"${jobs}" install DESTDIR="${pkgdir}"
    
    install -Dm 644 LICENSE "${pkgdir}/usr/share/licenses/${pkgname}/LICENSE"
    install -Dm 644 README.md "${pkgdir}/usr/share/doc/${pkgname}/README.md" || true
    
    install -d "${pkgdir}/usr/share/havel/scripts"
    install -d "${pkgdir}/usr/share/havel/extensions"
}
