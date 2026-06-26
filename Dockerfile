# Use a lightweight base image with the necessary build tools
FROM ubuntu:24.04 AS builder

# Set environment to non-interactive
ENV DEBIAN_FRONTEND=noninteractive

# Install build dependencies
RUN apt-get update && apt-get install -y \
    build-essential \
    cmake \
    clang \
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
    pkg-config \
    git \
    && rm -rf /var/lib/apt/lists/*

# Set working directory
WORKDIR /app

# Copy source code
COPY . .

# Build Havel (Mode 9: Release, no LLVM, no Qt)
# Adjusted to build without Qt, matching previous resolution
RUN chmod +x build.sh && \
    cmake -S . -B build-release -DENABLE_QT=OFF -DENABLE_QT_UI_BACKEND=OFF -DCMAKE_BUILD_TYPE=Release -DENABLE_HAVEL_LANG=ON -DENABLE_LLVM=OFF -DENABLE_GTK=OFF && \
    cmake --build build-release --target havel -j$(nproc)

# Final stage: minimal runtime environment
FROM ubuntu:24.04

# Install runtime dependencies
RUN apt-get update && apt-get install -y \
    libfmt-dev \
    libspdlog-dev \
    liblua5.4-0 \
    libtesseract5 \
    libleptonica6 \
    libopencv-core4.8 \
    libx11-6 \
    libxtst6 \
    libxrandr2 \
    libxinerama1 \
    libxcomposite1 \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /root/
COPY --from=builder /app/build-release/havel /usr/local/bin/havel

# Set default command
ENTRYPOINT ["havel"]
CMD ["--help"]
