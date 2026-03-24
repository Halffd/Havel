#!/bin/bash

echo "Testing signal handler in full mode..."

# Create a simple test script
cat > test_signal.hv << 'EOF'
print "Signal handler test running..."
print "Press Ctrl+C to test signal handling"
print "Script will run for 30 seconds"

# Simple loop that should be interrupted by signal
let count = 0
repeat 30 {
  print "Count: $count"
  count = count + 1
  sleep 1000  # Sleep 1 second
}

print "Script completed normally"
EOF

echo "Starting Havel in full mode with script..."
./build-debug/havel --full-repl test_signal.hv &
HAVEL_PID=$!

echo "Havel started with PID: $HAVEL_PID"

# Wait for startup
sleep 2

# Send SIGINT to test signal handler
echo "Sending SIGINT to Havel..."
kill -INT $HAVEL_PID

# Wait for process to exit
wait $HAVEL_PID
EXIT_CODE=$?

echo "Havel exited with code: $EXIT_CODE"

if [ $EXIT_CODE -eq 0 ]; then
    echo "✅ Signal handler working correctly in full mode"
else
    echo "❌ Signal handler may have issues in full mode (exit code: $EXIT_CODE)"
fi

# Clean up
rm -f test_signal.hv
