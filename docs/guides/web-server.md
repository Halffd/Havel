---
title: "Creating a Web Server"
description: "HTTP and socket capabilities: what exists, and what to use instead of a built-in web server."
---

# Creating a Web Server

**Status: there is no built-in HTTP server API.** Previous drafts of this
page documented `net.tcp.listen`, `net.ws.listen`, `net.tcp.listenTLS`,
`http.parseRequest`, and `http.formatResponse`. Those functions do not
exist — the `net` module is not loadable (`use net.*` fails), `http` is a
client-only module, and `http` has no request-parsing/formatting helpers.
Code using them will fail with `Unresolved identifier`.

This page documents what actually exists.

---

## What exists

### `http` — client only

`get`, `post`, `put`, `del`, `patch`, `head`, `upload`, `download`,
`urlEncode`, `urlDecode`, `isOnline`, returning
`{ status, ok, body, error, headers }` objects. See
[Host Functions Reference → HTTP](/reference/host-functions#http-http).

```hv
resp = http.get("https://example.com")
if resp.ok { print(resp.body) }
```

### `json` — via `use json`

Loads `modules/std/json.hv`. Verified members: `parse`, `parseFile`,
`parseString`, `parseNumber`, `parseArray`, `parseObject`, `parseValue`,
`parseNull`, `parseTrue`, `parseFalse`, `skipWS`, `stringify`.

```hv
use json
data = json.parse("{\"a\": 1}")
print(json.stringify(data))
```

`json.encode` / `json.decode` do not exist — the function names are
`json.stringify` and `json.parse`.

### `sqlite` — via `use sqlite`

Loads `modules/std/sqlite.hv`. Verified members: `open`, `Database`,
`memory`, `available`, plus `SQLITE_*` constants.

### `async_mod` — via `use async_mod`

Loads `modules/app/async_mod.hv`. Verified members include `sleep`,
`go`, `await`, `debounce`, `throttle`, `retry`, `withTimeout`, `race`,
`once`, `every`, `parallelMap`, `parallelFilter`, `parallelForEach`,
`promise`, `rateLimit`, `hasCircuitBreaker` family (`circuitBreaker`),
`allSettled`, `chan`, `fanIn`, `merge`, `then`.

`use async` (without the suffix) does not resolve.

### `socket` — via `use socket`

Loads `modules/std/socket.hv`: Unix-socket helpers (`create`, `connect`,
`send`, `recv`, `close`, `SOCKADDR_UN_SIZE`, `AF_UNIX`,
`SOCK_STREAM`, `UnixSocket`). There is no TCP listener in any module.

---

## Pattern: talk to an external server

The usual "web server in Havel" today is a viewer/controller script
talking to a real server process you spawn:

```hv
// start an external server
process.spawn("python3", ["-m", "http.server", "8080"])
sleep(300)

// drive it with the http client
resp = http.get("http://localhost:8080/")
print("${resp.status}: ${len(resp.body)} bytes")

process.spawn("pkill", ["-f", "http.server"])
```

The same pattern works for `nginx -c conf`, a FastAPI/Flask app, or any
other server binary — anything reachable via the `http` client or the
clipboard/window modules is scriptable.

For UI instead of HTTP, look at the GUI extensions under
`docs/stdlib/window.md` and the mode/statusbar tooling used by
`havel-wm` — neither requires a listening socket.

---

**Previous:** [Desktop Automation](/guides/desktop-automation)
**Next:** [Using FFI to Call C Libraries →](/guides/ffi)
