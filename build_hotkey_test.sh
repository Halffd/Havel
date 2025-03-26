#!/bin/bash
set -e

# Create build directory if it doesn't exist
mkdir -p build
cd build

# Configure and build
echo "Configuring CMake..."
cmake ..

echo "Building executables..."
cmake --build . --target hvc hotkey_test

# Check if build was successful
if [ $? -eq 0 ]; then
    echo "Build completed successfully."
    echo "Running hotkey test..."
    ./hotkey_test
else
    echo "Build failed. Please check the errors above."
    exit 1
fi

echo "Done." 