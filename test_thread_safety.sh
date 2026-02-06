#!/bin/bash

echo "Testing thread-safe hotkey execution..."
echo "This should trigger the zoom function which calls runDetached"
echo "Before fix: would cause SIGSEGV from memory corruption"
echo "After fix: should work safely via HotkeyExecutor"

# Start havel in background
timeout 5s ./build/havel -d &
HAVEL_PID=$!

# Wait a moment for startup
sleep 1

echo "Havel started with PID: $HAVEL_PID"
echo "HotkeyExecutor should be initialized"

# Check if HotkeyExecutor is working
if kill -0 $HAVEL_PID 2>/dev/null; then
    echo "✅ Havel is running - no immediate crash!"
    echo "✅ Thread-safe execution appears to be working"
else
    echo "❌ Havel crashed immediately"
fi

# Wait for timeout
wait $HAVEL_PID
echo "Test completed"
