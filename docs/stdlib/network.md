---
title: "Network Module"
description: "HTTP client and Unix socket operations."
---

# Network Module

The network functionality is split across two modules:
- `http` — HTTP client (GET, POST, PUT, DELETE, PATCH, download, upload)
- `socket` — Unix domain sockets (from `modules/app/socket.hv`)

---

## HTTP Module

```hv
use http
```

**Source**: `src/core/net/HttpModule.cpp` (C++ implementation using libcurl)

---

## HTTP Client

### Basic Methods

| Function | Signature | Description |
|----------|-----------|-------------|
| `http.get(url, headers?)` | `(str, object?) -> object` | HTTP GET |
| `http.post(url, data?, headers?)` | `(str, str?, object?) -> object` | HTTP POST |
| `http.put(url, data?, headers?)` | `(str, str?, object?) -> object` | HTTP PUT |
| `http.del(url, headers?)` | `(str, object?) -> object` | HTTP DELETE |
| `http.patch(url, data?, headers?)` | `(str, str?, object?) -> object` | HTTP PATCH |
| `http.head(url, headers?)` | `(str, object?) -> object` | HTTP HEAD |
| `http.isOnline()` | `() -> bool` | Check internet connectivity |
| `http.download(url, path)` | `(str, str) -> bool` | Download file to path |
| `http.upload(url, filePath, headers?)` | `(str, str, object?) -> object` | Upload file |
| `http.urlEncode(str)` | `(str) -> str` | URL encode string |
| `http.urlDecode(str)` | `(str) -> str` | URL decode string |
| `http.isOnline()` | `() -> bool` | Check internet connectivity |

```hv
resp = http.get("https://api.example.com/users")

resp = http.post("https://api.example.com/users", '{"name": "test"}')

resp = http.put("https://api.example.com/users/1", '{"name": "updated"}')

resp = http.del("https://api.example.com/users/1")
```

### Response Object

```hv
{
    statusCode: 200,           // HTTP status code
    statusText: "OK",          // Status text (content-type)
    body: "...",               // Response body as string
    headers: {                 // Response headers (lowercase keys)
        "content-type": "application/json",
        "server": "nginx"
    },
    error: ""                  // Empty on success, error message on failure
}
```

```hv
resp = http.get("https://api.example.com/users")
if resp.error == "" {
    print("Status: " + resp.statusCode)
    print("Body: " + resp.body)
    print("Content-Type: " + resp.headers["content-type"])
} else {
    print("Error: " + resp.error)
}
```

### Download

```hv
http.download(url, path)  // Download file to path
```

```hv
http.download("https://example.com/file.zip", "/tmp/file.zip")
```

### Upload

```hv
http.upload(url, filePath, headers?)  // Upload file
```

```hv
resp = http.upload("https://api.example.com/upload", "/path/to/file.pdf")
```

### Default Headers

```hv
http.setDefaultHeader("Authorization", "Bearer token")
http.setDefaultHeader("User-Agent", "MyApp/1.0")
http.clearDefaultHeaders()
```

### Query String Helper

```hv
params = { page: 1, limit: 10 }
url = "https://api.example.com/users?" + http.buildQueryString(params)
// "https://api.example.com/users?page=1&limit=10"
```

---

## Unix Socket Module

```hv
use socket
```

**Source**: `modules/app/socket.hv` (Havel FFI-based implementation)

---

## UnixSocket Class

```hv
sock = UnixSocket()
```

### Methods

| Method | Signature | Description |
|--------|-----------|-------------|
| `sock.connect(path)` | `(str) -> bool` | Connect to Unix socket |
| `sock.send(data)` | `(str) -> int` | Send data, returns bytes sent |
| `sock.recv(maxSize?)` | `(int?) -> str` | Receive data |
| `sock.close()` | `() -> nil` | Close connection |
| `sock.isAlive()` | `() -> bool` | Check if connected |

```hv
sock = UnixSocket()
if sock.connect("/tmp/my.sock") {
    sock.send("hello")
    response = sock.recv(1024)
    print("Received: " + response)
    sock.close()
}
```

### Standalone Functions

| Function | Signature | Description |
|----------|-----------|-------------|
| `socket.create()` | `() -> int` | Create socket fd |
| `socket.connect(fd, path)` | `(int, str) -> bool` | Connect fd to socket |
| `socket.send(fd, data)` | `(int, str) -> int` | Send on fd |
| `socket.recv(fd, maxSize?)` | `(int, int?) -> str` | Receive on fd |
| `socket.close(fd)` | `(int) -> nil` | Close fd |

```hv
fd = socket.create()
if socket.connect(fd, "/tmp/my.sock") {
    socket.send(fd, "hello")
    print(socket.recv(fd))
    socket.close(fd)
}
```

---

## Constants

```hv
AF_UNIX = 1
SOCK_STREAM = 1
```

---

## Example Usage

```hv
use http
use socket

// HTTP API call
resp = http.get("https://api.github.com/users/octocat")
if resp.error == "" {
    print(resp.body)
}

// Unix socket (e.g., mpv, docker)
sock = UnixSocket()
if sock.connect("/tmp/mpv-socket") {
    sock.send('{ "command": ["get_property", "volume"] }')
    print(sock.recv())
    sock.close()
}
```

---

**Previous:** [Shell Module](/stdlib/shell)
**Next:** [Clipboard Module →](/stdlib/clipboard)