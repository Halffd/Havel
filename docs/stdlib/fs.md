---
title: "Filesystem Module"
description: "File system operations: read, write, list, stat, watch, and path utilities."
---

# Filesystem Module

```hv
use fs
```

**Source**: `src/havel-lang/stdlib/FsModule.cpp` (C++ host functions)

---

## File Operations

| Function | Signature | Description |
|----------|-----------|-------------|
| `fs.read(path)` | `(str) -> str` | Read entire file as string |
| `fs.write(path, content)` | `(str, str) -> nil` | Write string to file |
| `fs.append(path, content)` | `(str, str) -> nil` | Append string to file |
| `fs.copy(src, dst)` | `(str, str) -> bool` | Copy file |
| `fs.move(src, dst)` | `(str, str) -> bool` | Move/rename file |
| `fs.delete(path)` | `(str) -> bool` | Delete file |
| `fs.exists(path)` | `(str) -> bool` | Check if path exists |

```hv
content = fs.read("file.txt")
fs.write("output.txt", "hello")
fs.append("log.txt", "new line\n")
fs.copy("src.txt", "dst.txt")
fs.move("old.txt", "new.txt")
fs.delete("temp.txt")
print(fs.exists("file.txt"))
```

---

## Directory Operations

| Function | Signature | Description |
|----------|-----------|-------------|
| `fs.mkdir(path)` | `(str) -> bool` | Create directory (recursive) |
| `fs.rmdir(path)` | `(str) -> bool` | Remove empty directory |
| `fs.removeAll(path)` | `(str) -> bool` | Remove directory recursively |
| `fs.list(path)` | `(str) -> array` | List directory entries (file objects) |

```hv
fs.mkdir("path/to/dir")
fs.list(".")
// Returns array of file objects with: name, path, extension, size, modified, isDir, isFile, isSymlink, permissions, etc.
```

---

## File/Directory Info

| Function | Signature | Description |
|----------|-----------|-------------|
| `fs.stat(path)` | `(str) -> object?` | Get file info object |
| `fs.isDir(path)` | `(str) -> bool` | Check if directory |
| `fs.isFile(path)` | `(str) -> bool` | Check if regular file |
| `fs.isSymlink(path)` | `(str) -> bool` | Check if symlink |

File object fields:
```hv
{
  name: "file.txt",
  path: "/full/path/file.txt",
  extension: "txt",
  size: 1024,
  modified: 1700000000,  // Unix timestamp
  access: 1700000000,
  birthDate: 1700000000,
  permissions: 0o644,
  isDir: false,
  isFile: true,
  isSymlink: false,
}
```

```hv
info = fs.stat("file.txt")
print(info.size)
print(info.modified)
```

---

## Path Utilities

| Function | Signature | Description |
|----------|-----------|-------------|
| `fs.join(...parts)` | `(...str) -> str` | Join path parts |
| `fs.resolve(path)` | `(str) -> str` | Resolve to absolute path |
| `fs.relative(from, to)` | `(str, str) -> str` | Relative path from `from` to `to` |
| `fs.dirname(path)` | `(str) -> str` | Directory name |
| `fs.basename(path)` | `(str) -> str` | File name |
| `fs.extname(path)` | `(str) -> str` | Extension (with dot) |
| `fs.cwd()` | `() -> str` | Current working directory |
| `fs.chdir(path)` | `(str) -> bool` | Change working directory |

```hv
fs.join("a", "b", "c")       // "a/b/c"
fs.resolve("file.txt")       // "/home/user/file.txt"
fs.dirname("/a/b/file.txt")  // "/a/b"
fs.basename("/a/b/file.txt") // "file.txt"
fs.extname("file.txt")       // ".txt"
```

---

## File Watching

| Function | Signature | Description |
|----------|-----------|-------------|
| `fs.watch(path, callback)` | `(str, fn) -> int` | Watch file/dir for changes, returns watcher ID |
| `fs.unwatch(id)` | `(int) -> nil` | Stop watching |

```hv
watcher = fs.watch(".", fn(event) {
    print("Changed: " + event.path + " " + event.type)
})
# ... later ...
fs.unwatch(watcher)
```

Event object:
```hv
{
  path: "file.txt",
  type: "modify"  // "modify", "create", "delete", "rename"
}
```

---

## Temporary Files

| Function | Signature | Description |
|----------|-----------|-------------|
| `fs.tempDir()` | `() -> str` | System temp directory |
| `fs.tempFile(prefix?)` | `(str?) -> str` | Create temp file, returns path |

```hv
print(fs.tempDir())
tmp = fs.tempFile("myapp_")
fs.write(tmp, "temporary data")
```

---

## Example Usage

```hv
use fs

// Read/write
content = fs.read("config.json")
fs.write("output.json", content)

// Directory
fs.mkdir("data/logs")
for entry in fs.list("data") {
    print(entry.name + " (" + (entry.isDir ? "dir" : "file") + ")")
}

// Path operations
full = fs.join(fs.cwd(), "file.txt")
print(fs.dirname(full))
print(fs.basename(full))

// Watch
watcher = fs.watch("config.json", fn(e) {
    print("Config changed: " + e.type)
})
```

---

**Previous:** [Time Module](/stdlib/time)
**Next:** [Shell Module →](/stdlib/shell)