#!/bin/bash

echo "Testing signal handler..."

# Start havel in background
./build-debug/havel --repl &
HAVEL_PID=$!

echo "Havel started with PID: $HAVEL_PID"

# Wait a moment for startup
sleep 2

# Send SIGTERM to test signal handler
echo "Sending SIGTERM to Havel..."
kill -TERM $HAVEL_PID

# Wait for process to exit
wait $HAVEL_PID
EXIT_CODE=$?

echo "Havel exited with code: $EXIT_CODE"

if [ $EXIT_CODE -eq 0 ]; then
    echo "✅ Signal handler working correctly"
else
    echo "❌ Signal handler may have issues"
fi
