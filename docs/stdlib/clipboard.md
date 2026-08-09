---
title: "Clipboard Module"
description: "Clipboard operations: get/set text, images, files, and clipboard info."
---

# Clipboard Module

```hv
use clipboard
```

**Source**: `src/modules/clipboard/ClipboardModule.cpp` (C++ host functions via ClipboardService)

---

## Text Operations

| Function | Signature | Description |
|----------|-----------|-------------|
| `clipboard.get()` | `() -> str` | Get clipboard text (alias for getText) |
| `clipboard.getText()` | `() -> str` | Get clipboard text |
| `clipboard.set(text)` | `(str) -> bool` | Set clipboard text (alias for setText) |
| `clipboard.setText(text)` | `(str) -> bool` | Set clipboard text |
| `clipboard.clear()` | `() -> bool` | Clear clipboard |
| `clipboard.hasText()` | `() -> bool` | Check if clipboard has text |

```hv
text = clipboard.get()
clipboard.set("hello world")

if clipboard.hasText() {
    print(clipboard.getText())
}
```

---

## Clipboard Info

```hv
clipboard.out()  // -> object
```

Returns detailed clipboard information:

```hv
{
    type: "text" | "image" | "files" | "empty",
    content: "clipboard text",
    size: 1234,           // bytes
    mimeType: "text/plain",
    files: ["path1", "path2"],
    isText: true,
    isImage: false,
    isFiles: false,
    isEmpty: false
}
```

```hv
info = clipboard.out()
print("Type: " + info.type)
print("Content: " + info.content)
```

---

## Image Operations

| Function | Signature | Description |
|----------|-----------|-------------|
| `clipboard.hasImage()` | `() -> bool` | Check if clipboard has image |
| `clipboard.getImage()` | `() -> str` | Get image as base64 string |
| `clipboard.setImage(data)` | `(str) -> bool` | Set image from base64 string |

```hv
if clipboard.hasImage() {
    img = clipboard.getImage()
    // img is base64 encoded
}
```

---

## Files Operations

| Function | Signature | Description |
|----------|-----------|-------------|
| `clipboard.hasFiles()` | `() -> bool` | Check if clipboard has files |
| `clipboard.getFiles()` | `() -> array` | Get file paths array |
| `clipboard.setFiles(paths)` | `(array|str) -> bool` | Set files from array or single path |

```hv
if clipboard.hasFiles() {
    files = clipboard.getFiles()
    for f in files { print(f) }
}

clipboard.setFiles(["/path/to/file1.txt", "/path/to/file2.txt"])
```

---

## Backend Methods

| Function | Signature | Description |
|----------|-----------|-------------|
| `clipboard.setMethod(method)` | `(str) -> bool` | Set backend: "qt", "x11", "wayland", "external", "windows", "macos", "auto" |
| `clipboard.getMethod()` | `() -> str` | Get current backend |
| `clipboard.detectMethod()` | `() -> str` | Auto-detect best backend |

```hv
clipboard.setMethod("x11")
print(clipboard.getMethod())  // "x11"
print(clipboard.detectMethod())  // "x11"
```

---

## Example Usage

```hv
use clipboard

// Text
clipboard.set("Hello from Havel!")
print("Clipboard: " + clipboard.get())

// Full info
info = clipboard.out()
print("Type: " + info.type)
print("Content: " + info.content)

// Files
clipboard.setFiles(["/home/user/file1.txt", "/home/user/file2.txt"])
files = clipboard.getFiles()
for f in files { print(f) }

// Image (base64)
if clipboard.hasImage() {
    img = clipboard.getImage()
    // process image...
}
```

---

**Previous:** [Network Module](/stdlib/network)
**Next:** [Window Module →](/stdlib/window)