---
title: "Process Module"
description: "Run commands, capture output, pipe, find and signal processes. Shell strings and argv vectors, with exit-status semantics."
---

# Process Module

```hv
use process
```

**Source**: `modules/std/process.hv` (Havel layer) plus the native
`process.*` / `__proc.*` host functions from
`src/havel-lang/stdlib/SysModule.cpp`, `src/core/process/Launcher.cpp` and
`src/host/module/bridges/SystemBridge.cpp`.

The native `process` namespace owns the primitives (`run`, `pipe`, `spawn`,
`wait`, `find`, `exists`, `kill`, `nice`, `pid`, `ppid`, `getState`,
`runDetached`, `killObj`, `exit`), and importing this module **merges** its
convenience layer into that same object. So `use { capture } from "process"`
and `process.capture` are the same function — there is no second namespace
to keep in sync.

---

## The string / array contract

Every primitive takes its command one of two ways, and the difference is
not cosmetic:

| Form | Meaning |
|------|---------|
| **`str`** | A shell command line. Run through `/bin/sh -c`, so pipes, redirection, globs and quoting all work: `"grep -c err log.txt \| wc -l"`. |
| **`array`** | An argv vector exec'd **directly**. No word splitting and no globbing, so an argument containing spaces stays one argument: `["mpv", "--title=My Movie", "file.mp4"]`. |

Which to pick is a correctness question, not style. An array cannot be
surprised by a space in a filename; a string is the only way to express a
pipeline without building the pipes yourself.

### `success` follows the exit status

`success` means **the command ran and exited 0**. A command that ran
perfectly and exited non-zero has `success == false` — that is the whole
value of the field. It does *not* mean "the spawn worked".

---

## Running

| Function | Signature | Description |
|----------|-----------|-------------|
| `process.run(cmd)` | `(str \| array) -> obj` | Run synchronously. Returns the result object below. |
| `process.capture(cmd)` | `(str \| array) -> str` | Run, return trimmed stdout. A failure is logged, not thrown. |
| `process.runOrThrow(cmd)` | `(str \| array) -> obj` | Run, throw with the exit code and stderr on failure. |
| `process.ok(cmd)` | `(str \| array) -> bool` | Run silently, `true` on exit 0. |
| `process.exec(cmd)` | `(str \| array) -> int` | Spawn + wait, return the exit code only. |
| `process.output(cmd)` | `(str \| array) -> obj` | Spawn + wait, return `{stdout, stderr, exitCode}`. |
| `process.exit(code)` | `(int) -> nil` | Exit this process cleanly. |

```hv
r = process.run("echo hello")
print(r.success)     // true
print(r.exitCode)    // 0
print(r.stdout)      // "hello\n"

// An argv array keeps the spaces in one argument:
r = process.run(["echo", "hello world"])
print(r.stdout.trim())   // "hello world"

// Non-zero exit: still a result, not an exception.
r = process.run("exit 3")
print(r.success)     // false
print(r.exitCode)    // 3

print(process.capture("date +%Y"))
print(process.ok("true"), process.ok("false"))   // true false
```

### The result object

```hv
{
  pid: 12345,          // child pid, -1 when the spawn failed
  exitCode: 0,         // 0 on success; -N for a child killed by signal N
  success: false,      // error.empty() && exitCode == 0
  stdout: "...",       // captured stdout, unmodified
  stderr: "...",       // captured stderr, unmodified
  error: null,         // why it failed, or null.
                       // "process.wait() is not supported on Windows",
                       // "Empty executable path", a spawn failure message
}
```

---

## Spawn and wait (two-step)

For long-running or interactive children, `spawn` hands you the pid and
the read ends of the pipes, and `wait` collects it later. The pipes are
drained **while** the child runs, so a child writing more than the 64 KiB
pipe buffer cannot deadlock the wait — the child would block in `write()`
while you block in `waitpid()`.

| Function | Signature | Description |
|----------|-----------|-------------|
| `process.spawn(cmd)` | `(str \| array) -> obj` | Start, return `{pid, exitCode, ...}` with `exitCode` still running. |
| `process.wait(proc)` | `(obj) -> obj` | Wait for it, fill in `exitCode`, `stdout`, `stderr`, `success`. |
| `process.killObj(proc)` | `(obj) -> nil` | Kill a spawned handle. |

```hv
p = process.spawn(["sleep", "30"])
print(p.pid)             // the child's pid
// ... do other work ...
done = process.wait(p)
print(done.exitCode)     // 0
```

---

## Pipelines

Two different pipelines, two different names:

| Function | Signature | Description |
|----------|-----------|-------------|
| `process.pipe(stages)` | `(array) -> obj` | **Native, argv-based.** A flat argv is one command; a list of argv arrays chains each stage's stdout into the next. Returns the **last** stage's result object. Real pipes, no temp files. |
| `process.pipeShell(cmd1, cmd2)` | `(str, str) -> obj` | **Shell strings.** Runs `cmd1`, feeds its stdout verbatim into `cmd2`. |

`pipeShell` is deliberately not called `pipe`: the native `pipe` owns the
plain name and takes argv vectors, so having both under one name meant
callers and the module disagreed about the argument type.

`pipeShell` feeds the first command's stdout through `printf '%s'`, not
`echo` — `echo` appends a newline, so the second command would see one
more line than the first produced.

```hv
// argv pipeline: no shell, no word splitting
r = process.pipe([["grep", "error", "app.log"], ["wc", "-l"]])
print(r.stdout.trim())

// optional stdin for the first stage
r = process.pipe([["cat"], ["wc", "-l"]], "one\ntwo\nthree\n")

// shell pipeline
r = process.pipeShell("ls /etc", "grep '^passwd'")

// exit codes: a command that ran and exited non-zero is success == false
print(process.pipe([["false"], ["true"]]).success)   // false
```

---

## Finding and signalling

| Function | Signature | Description |
|----------|-----------|-------------|
| `process.find(name)` | `(str) -> array` | PIDs whose process name matches. |
| `process.exists(nameOrPid)` | `(str \| int) -> bool` | Is it running? |
| `process.kill(pid, sig)` | `(int, int \| str) -> bool` | Send a signal. |
| `process.sendSignal(pid, sig)` | `(int, int \| str) -> bool` | Alias of `kill`. |
| `process.getState(pid)` | `(int) -> str` | Process state. |
| `process.nice(pid, value)` | `(int, int) -> bool` | Set the nice value. |
| `process.pid()` | `() -> int` | This process. **A function — call it.** |
| `process.ppid()` | `() -> int` | This process's parent. |

Signal constants are plain integers on the namespace:

```hv
SIGKILL = 9   SIGTERM = 15  SIGHUP = 1   SIGINT = 2
SIGSTOP = 19  SIGCONT = 18
```

```hv
if process.exists("mpv") {
  for pid in process.find("mpv") {
    process.kill(pid, process.SIGTERM)
  }
}
```

`process.find` races the child's `exec` — a freshly detached process may
not be visible yet. Poll rather than sampling once.

---

## `process.exit`

```hv
process.exit(0)
```

Routes through the cooperative `sys.exit` host function, never libc
`exit()` via FFI. Raw `exit()` mid-goroutine runs static destructors while
the EventListener thread and parked hotkey fibers are alive, and the
process deadlocks in thread joins.

---

## Gotchas worth knowing

- `process.run` returns a result object even on failure. Only
  `runOrThrow` throws, and `capture` logs.
- `process.pid` / `process.ppid` are **functions**. Reading them without
  `()` gives you the function.
- An argument with spaces in it means you want an **array** command, not a
  quoted string — quoting is the shell's job and a string is the shell.
- Tests that spawn real processes live in `scripts/tests/process/` and are
  run explicitly through `hvtest`; they are deliberately **not** collected
  by the smoke suite.

**Previous:** [Filesystem Module](/stdlib/fs)
**Next:** [Shell Module →](/stdlib/shell)
**Next:** [Shell Module →](/stdlib/shell)