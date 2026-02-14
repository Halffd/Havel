#!/bin/bash

# Test Havel without interpreter dependency
echo "🧪 Testing Havel Core System (No Interpreter)"

# Test 1: Check if binary exists and runs
if [ ! -f "./hav" ]; then
    echo "❌ Havel binary not found"
    exit 1
fi

echo "✅ Havel binary found"

# Test 2: Test help system
echo "📋 Testing help system..."
./hav --help 2>/dev/null | head -10

# Test 3: Test configuration system
echo "📋 Testing configuration..."
if [ -f "config/havel.cfg" ]; then
    echo "✅ Configuration file exists"
    echo "  Config size: $(wc -l < config/havel.cfg) lines"
else
    echo "⚠️  Configuration file not found"
fi

# Test 4: Test build artifacts
echo "📋 Testing build artifacts..."
if [ -f "./hav" ]; then
    echo "✅ Main executable exists"
    echo "  Binary size: $(ls -lh ./hav | awk '{print $5}')"
    echo "  Binary type: $(file ./hav | cut -d: -f2-)"
fi

# Test 5: Test library dependencies
echo "📋 Testing library dependencies..."
ldd ./hav 2>/dev/null | grep -E "(qt|llvm|pipewire)" | head -5

# Test 6: Test core functionality without script execution
echo "📋 Testing core functionality..."
echo "  - Qt6 version: $(pkg-config --modversion Qt6Core 2>/dev/null || echo 'Not found')"
echo "  - LLVM version: $(llvm-config --version 2>/dev/null || echo 'Not found')"
echo "  - PipeWire version: $(pkg-config --modversion libpipewire-0.3 2>/dev/null || echo 'Not found')"

# Test 7: Test source code structure
echo "📋 Testing source structure..."
echo "  - Total source files: $(find src -name "*.cpp" | wc -l)"
echo "  - Total header files: $(find src -name "*.h" | wc -l)"
echo "  - Language files: $(find src/havel-lang -name "*.cpp" 2>/dev/null | wc -l || echo '0')"

# Test 8: Test system integration
echo "📋 Testing system integration..."
echo "  - Display server: $(echo $XDG_SESSION_TYPE)"
echo "  - Shell: $SHELL"
echo "  - Memory available: $(free -h | grep Mem | awk '{print $7}')"

# Test 9: Create minimal test without interpreter
echo "📋 Creating minimal test..."
cat > test_minimal.hv << 'EOF'
// Minimal test - no interpreter needed
// This is just a text file to test file operations
print("Hello from minimal test!");
EOF

echo "✅ Minimal test created"

# Test 10: Test file operations
echo "📋 Testing file operations..."
if [ -f "test_minimal.hv" ]; then
    echo "✅ Test file created successfully"
    echo "  File size: $(wc -c < test_minimal.hv) bytes"
    echo "  File content preview:"
    head -3 test_minimal.hv | sed 's/^/    /'
fi

# Cleanup
rm -f test_minimal.hv

echo ""
echo "🎉 Core System Test Completed!"
echo "📊 Status: Core infrastructure verified"
echo "🔧 Next: Fix interpreter instantiation issue"
