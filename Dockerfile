# Build stage
FROM ubuntu:24.04 AS builder

ENV DEBIAN_FRONTEND=noninteractive

# Install build dependencies
RUN apt-get update && apt-get install -y \
    build-essential \
    cmake \
    clang \
    lld \
    libfmt-dev \
    nlohmann-json3-dev \
    libspdlog-dev \
    liblua5.4-dev \
    libtesseract-dev \
    libleptonica-dev \
    libopencv-dev \
    libx11-dev \
    libxtst-dev \
    libxrandr-dev \
    libxinerama-dev \
    libxcomposite-dev \
    libxi-dev \
    libxfixes-dev \
    libxdamage-dev \
    libwayland-dev \
    wayland-protocols \
    libxkbcommon-dev \
    libpcre2-dev \
    libgl-dev \
    libdbus-1-dev \
    libssl-dev \
    libzip-dev \
    pkg-config \
    git \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /app
COPY . .

# Build Havel (Release, portable, no LLVM, no Qt, with Havel Lang and module plugins)
RUN chmod +x build.sh && \
    cmake -S . -B build-release \
        -DCMAKE_BUILD_TYPE=Release \
        -DPORTABLE_BUILD=ON \
        -DENABLE_LLVM=OFF \
        -DENABLE_QT=OFF \
        -DENABLE_QT_UI_BACKEND=OFF \
        -DENABLE_GTK=OFF \
        -DENABLE_HAVEL_LANG=ON \
        -DENABLE_MODULE_PLUGINS=ON \
        -DENABLE_TESTS=OFF \
        -DCMAKE_C_COMPILER=clang \
        -DCMAKE_CXX_COMPILER=clang++ \
        -DCMAKE_EXE_LINKER_FLAGS="-fuse-ld=lld" \
        -DCMAKE_SHARED_LINKER_FLAGS="-fuse-ld=lld" \
        -DCMAKE_MODULE_LINKER_FLAGS="-fuse-ld=lld" && \
    cmake --build build-release -j$(nproc) && \
    cmake --build build-release --target gamma_ramp && \
    cmake --build build-release --target compile-stdlib-bytecode

# Runtime stage
FROM ubuntu:24.04 AS runtime

ENV DEBIAN_FRONTEND=noninteractive

# Install runtime dependencies (Ubuntu 24.04 noble package names)
RUN apt-get update && apt-get install -y \
    libfmt9 \
    libspdlog1.12 \
    liblua5.4-0 \
    libtesseract5 \
    liblept5 \
    libopencv-core406t64 \
    libopencv-imgproc406t64 \
    libopencv-imgcodecs406t64 \
    libx11-6 \
    libxtst6 \
    libxrandr2 \
    libxinerama1 \
    libxcomposite1 \
    libxi6 \
    libxfixes3 \
    libxdamage1 \
    libwayland-client0 \
    libxkbcommon0 \
    libpcre2-8-0 \
    libpcre2-16-0 \
    libcurl4 \
    libpipewire-0.3-0t64 \
    libasound2t64 \
    libreadline8t64 \
    libncurses6 \
    libffi8 \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /root/

# Copy havel binary
COPY --from=builder /app/build-release/havel /usr/local/bin/havel

# Copy precompiled bytecode and module sources
COPY --from=builder /app/build-release/share/havel /usr/share/havel

# Copy module plugins
COPY --from=builder /app/build-release/modules /usr/lib/havel/modules

# Copy gamma_ramp extension
COPY --from=builder /app/build-release/libgamma_ramp.so /usr/lib/havel/libgamma_ramp.so

# Set default command
ENTRYPOINT ["havel"]
CMD ["--help"]