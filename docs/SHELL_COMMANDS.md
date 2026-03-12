# Shell Command Support

Havel supports shell command execution with `$` and `$!` operators, plus process control functions.

## Quick Reference

```havel
// Run and forget (fire and forget)
$ firefox

// Run and capture output
let out = $! ls

// Process control
process.run("firefox")      // Run and get PID
process.find("firefox")     // Find processes
process.kill(pid)           // Kill process
```

## Shell Command Syntax

### `$ command` - Run and return exit code
```havel
$ firefox        // Run firefox, return exit code
$ ls -la         // Run ls, return exit code
```

### `$! command` - Run and capture stdout
```havel
let out = $! ls           // Capture ls output
let files = $! ls *.txt   // Capture filtered output
print(out)                // Print captured output
```

### `$! [array]` - Run with arguments (no shell)
```havel
$! ["ls", "-la", "/tmp"]  // Run ls without shell
```

### `$! var` - Run command from variable
```havel
let cmd = "echo hello"
let out = $! cmd
print(out)  // Prints: hello
```

### `$! "string"` - Run command from string literal
```havel
let out = $! "echo hello"
print(out)  // Prints: hello
```

### Piping commands
```havel
$! ls | grep ".txt" | wc -l  // Count txt files
```

## Process Control

### `process.run(command)` - Run and get PID
```havel
let pid = process.run("firefox")
print("Firefox PID:", pid)
```

### `process.find(name)` - Find processes by name
```havel
let procs = process.find("firefox")
for proc in procs {
    print("Found:", proc.name, "PID:", proc.pid)
}
```

### `process.list()` - List all processes
```havel
let all = process.list()
for proc in all {
    print(proc.pid, proc.name)
}
```

### `process.kill(pid)` - Kill process
```havel
process.kill(pid)
```

### `process.send(pid, signal)` - Send signal
```havel
process.send(pid, 9)   // SIGKILL
process.send(pid, 15)  // SIGTERM
```

### `process.alive(pid)` - Check if process is running
```havel
if process.alive(pid) {
    print("Still running")
}
```

### `process.wait(pid)` - Wait for process to exit
```havel
process.wait(pid)
```

## Examples

```havel
// Run a GUI application
$ firefox

// Capture command output
let files = $! ls
for file in files.split("\n") {
    print(file)
}

// Run with process control
let pid = process.run("firefox")
print("Started firefox with PID:", pid)

// Find and kill processes
let procs = process.find("zombie")
for proc in procs {
    process.kill(proc.pid)
}

// Pipe commands
let count = $! ps aux | grep "havel" | wc -l
print("Havel processes:", count)

// Monitor a process
let pid = process.run("sleep 10")
while process.alive(pid) {
    print("Still running...")
    sleep(1000)
}
print("Process finished")
```

## Implementation

- `$` - Returns exit code (statement form)
- `$!` - Captures stdout as string (expression form)
- Commands are executed via `havel::Launcher::runShell()`
- Array form uses `havel::Launcher::run()` without shell
- Process functions use `ProcessManager` class

## Notes

- Shell commands are parsed as statements or expressions
- `$` without `!` returns exit code
- `$!` captures stdout as string
- Array form avoids shell injection risks
- Piping supported with `|` operator
- Process control provides more fine-grained control
