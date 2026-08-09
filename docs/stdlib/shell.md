---
title: "Shell Module"
description: "Shell command execution, environment, filesystem operations, I/O, and process control."
---

# Shell Module

```hv
use shell
```

**Source**: `src/havel-lang/stdlib/ShellModule.cpp` (C++ host functions) + `modules/app/shell.hv` (Havel sidecar)

---

## Command Execution

| Function | Signature | Description |
|----------|-----------|-------------|
| `shell.run(cmd)` | `(str) -> int` | Run command detached, returns PID (-1 on failure) |
| `shell.exec(cmd)` | `(str) -> object` | Execute command, capture output |

```hv
pid = shell.run("firefox &")
result = shell.exec("ls -la")
print(result.stdout)
print(result.exitCode)
```

**Result object:**
```hv
{
    stdout: "output text",
    stderr: "error text",
    ok: true,
    exitCode: 0
}
```

### Command Validation

Commands are validated against an allowlist and checked for dangerous patterns (command injection prevention).

```hv
// Allowed commands: ls, cat, echo, grep, find, wc, head, tail,
// mkdir, rmdir, cp, mv, rm, touch, stat, ps, df, du, free,
// uptime, whoami, id, date, cal, sleep, sort, uniq, cut, tr,
// awk, sed, tee, xargs, which, whereis, git, cargo, npm, make,
// cmake, clang, gcc, python3, python, node, deno, bun,
// ssh, scp, rsync, curl, wget, ping, dig, tar, gzip, gunzip,
// zip, unzip, bzip2, bunzip2
```

Dangerous patterns rejected: `;`, `&&`, `||`, `\|`, `` ` ``, `$()`, `${`, `>`, `<`, `>>`, `2>`, `&>`, `exec`, `eval`, `source`, `. `

---

## Command Utilities

| Function | Signature | Description |
|----------|-----------|-------------|
| `shell.which(cmd)` | `(str) -> str?` | Find executable in PATH |
| `shell.escape(arg)` | `(str) -> str` | Shell-safe quoting |
| `shell.splitArgs(cmd)` | `(str) -> array` | Split command into args |
| `shell.open(path)` | `(str) -> nil` | Open file/URL with default handler |

```hv
shell.escape("file with spaces.txt")  // "file\\ with\\ spaces.txt"
shell.splitArgs("ls -la")            // ["ls", "-la"]
print(shell.which("python3"))        // "/usr/bin/python3"
```

---

## Environment

| Function | Signature | Description |
|----------|-----------|-------------|
| `shell.env(name, value?)` | `(str, str?) -> str?` | Get or set environment variable |
| `shell.getenv(name)` | `(str) -> str?` | Get environment variable (readonly) |
| `shell.envList()` | `() -> object` | All environment variables |

```hv
shell.env("MY_VAR", "value")
print(shell.env("PATH"))
print(shell.envList().HOME)
```

---

## Working Directory

| Function | Signature | Description |
|----------|-----------|-------------|
| `shell.cwd()` | `() -> str` | Current working directory |
| `shell.cd(path)` | `(str) -> bool` | Change directory |

```hv
print(shell.cwd())
shell.cd("/tmp")
```

---

## Process Info

| Function | Signature | Description |
|----------|-----------|-------------|
| `shell.pid()` | `() -> int` | Current process ID |
| `shell.exit(code)` | `(int) -> never` | Exit process |

```hv
print(shell.pid())
shell.exit(0)
```

---

## System Info

| Function | Signature | Description |
|----------|-----------|-------------|
| `shell.platform()` | `() -> str` | "linux", "windows", "macos", "freebsd", etc. |
| `shell.hostname()` | `() -> str` | Hostname |
| `shell.user()` | `() -> str` | Username |
| `shell.home()` | `() -> str` | Home directory |
| `shell.tmpdir()` | `() -> str` | Temp directory |
| `shell.shell()` | `() -> str` | Default shell path |

```hv
print(shell.platform())  // "linux"
print(shell.home())      // "/home/user"
```

---

## Standard I/O

| Function | Signature | Description |
|----------|-----------|-------------|
| `shell.read()` | `() -> str?` | Read line from stdin |
| `shell.write(text, fd?)` | `(str, int?) -> nil` | Write to stdout (1) or stderr (2) |
| `shell.isatty(fd)` | `(int) -> bool` | Check if fd is a terminal (0=stdin, 1=stdout, 2=stderr) |
| `shell.ready(timeoutMs?)` | `(int?) -> bool` | Non-blocking stdin check |

```hv
name = shell.read()              // Read line from stdin
shell.write("Hello, {name}!\n")  // Write to stdout
shell.write("Error!\n", 2)       // Write to stderr (fd=2)

print(shell.isatty(0))   // stdin
print(shell.isatty(1))   // stdout

# Non-blocking input check
if shell.ready(100) {    // Check with 100ms timeout
    line = shell.read()
    print("Got: " + line)
}
```

---

## Filesystem Operations

| Function | Signature | Description |
|----------|-----------|-------------|
| `shell.exists(path)` | `(str) -> bool` | Path exists |
| `shell.isFile(path)` | `(str) -> bool` | Is regular file |
| `shell.isDir(path)` | `(str) -> bool` | Is directory |
| `shell.mkdir(path)` | `(str) -> bool` | Create directory |
| `shell.mkdirs(path)` | `(str) -> bool` | Create directory recursively |
| `shell.remove(path)` | `(str) -> bool` | Delete file/empty dir |
| `shell.removeAll(path)` | `(str) -> int` | Delete recursively (returns count) |
| `shell.copy(src, dst)` | `(str, str) -> bool` | Copy file |
| `shell.move(src, dst)` | `(str, str) -> bool` | Move/rename |
| `shell.listDir(path)` | `(str) -> array` | List directory |
| `shell.tmpfile()` | `() -> str?` | Create temp file |

```hv
shell.mkdir("newdir")
shell.mkdirs("path/to/nested")
for f in shell.listDir(".") { print(f) }
```

**Note**: All filesystem operations are restricted to allowed directories (cwd, home, temp).

---

## Sleep

```hv
shell.sleep(seconds)  // supports fractional seconds
```

---

## History

| Function | Signature | Description |
|----------|-----------|-------------|
| `shell.history_path()` | `() -> str?` | History file path (~/.havel_history) |
| `shell.history_read(path?)` | `(str?) -> array` | Read history lines |
| `shell.history_write(arr, path?)` | `(array, str?) -> nil` | Write history |
| `shell.history_add(line, path?)` | `(str, str?) -> nil` | Append to history |

```hv
shell.history_add("my command")
history = shell.history_read()
```

---

## Example Usage

```hv
use shell

// Execute and capture
result = shell.exec("ls -la")
if result.ok {
    print(result.stdout)
} else {
    print("Error: " + result.stderr)
}

// Environment
shell.env("MY_VAR", "test")
print(shell.env("MY_VAR"))

// Filesystem
shell.mkdir("output")
shell.write("data.txt", "content")

// Process info
print("PID: " + shell.pid())
print("Platform: " + shell.platform())
```

---

**Previous:** [Filesystem Module](/stdlib/fs)
**Next:** [Network/HTTP Module →](/stdlib/network)