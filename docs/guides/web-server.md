---
title: "Creating a Web Server"
description: "Build an HTTP server with routing, middleware, and WebSocket support."
---

# Creating a Web Server

## Basic HTTP Server

```hv
// server.hv
use http
use net

fn handle_request(req) {
    match req.path {
        "/" => { status: 200, body: "Hello, World!", headers: { "Content-Type": "text/plain" } }
        "/health" => { status: 200, body: "OK", headers: { "Content-Type": "text/plain" } }
        "/api/users" => { 
            status: 200, 
            body: json.encode([{ id: 1, name: "Alice" }, { id: 2, name: "Bob" }]),
            headers: { "Content-Type": "application/json" }
        }
        _ => { status: 404, body: "Not Found" }
    }
}

server = net.tcp.listen(8080, fn(conn) {
    request = http.parseRequest(conn.recv(4096))
    response = handle_request(request)
    conn.send(http.formatResponse(response))
    conn.close()
})

print("Server running on http://localhost:8080")
```

Run: `./build-debug/havel server.hv`

Test: `curl http://localhost:8080/`

---

## Routing with Middleware

```hv
// router.hv
use http

// Middleware type: fn(req, next) -> response
fn logging_middleware(req, next) {
    start = time.now()
    resp = next(req)
    duration = time.now() - start
    print("{req.method} {req.path} -> {resp.status} ({duration}ms)")
    resp
}

fn cors_middleware(req, next) {
    resp = next(req)
    resp.headers["Access-Control-Allow-Origin"] = "*"
    resp.headers["Access-Control-Allow-Methods"] = "GET, POST, PUT, DELETE, OPTIONS"
    resp.headers["Access-Control-Allow-Headers"] = "Content-Type, Authorization"
    resp
}

fn auth_middleware(req, next) {
    token = req.headers["Authorization"]
    if !token || !token.startsWith("Bearer ") {
        return { status: 401, body: "Unauthorized" }
    }
    req.user = validate_token(token.split(" ")[1])
    next(req)
}

// Router
routes = {
    "GET": {
        "/": fn(req) => { status: 200, body: "Home" },
        "/health": fn(req) => { status: 200, body: "OK" },
        "/api/users": [auth_middleware, fn(req) => { 
            status: 200, 
            body: json.encode(users),
            headers: { "Content-Type": "application/json" }
        }],
        "/api/users/:id": [auth_middleware, fn(req) => {
            user = users.find(u => u.id == req.params.id)
            if user { { status: 200, body: json.encode(user), headers: { "Content-Type": "application/json" } } }
            else { { status: 404, body: "Not Found" } }
        }]
    },
    "POST": {
        "/api/users": [auth_middleware, fn(req) => {
            user = json.decode(req.body)
            user.id = next_id++
            users.push(user)
            { status: 201, body: json.encode(user), headers: { "Content-Type": "application/json" } }
        }]
    }
}

// Apply middleware chain
fn apply_middleware(req, handler) {
    chain = [cors_middleware, logging_middleware]
    fn run_chain(i, req) {
        if i >= chain.len() { return handler(req) }
        chain[i](req, fn(r) => run_chain(i + 1, r))
    }
    run_chain(0, req)
}

// Dispatch
fn dispatch(req) {
    method_routes = routes[req.method] || {}
    for pattern, handler in method_routes {
        match = match_route(pattern, req.path)
        if match {
            req.params = match.params
            return apply_middleware(req, handler)
        }
    }
    { status: 404, body: "Not Found" }
}

// Route matching with params
fn match_route(pattern, path) {
    // Simple implementation: /api/users/:id -> regex
    regex_pattern = pattern.replace(":id", "([^/]+)") + "$"
    m = regex.match("^" + regex_pattern, path)
    if m {
        params = {}
        if pattern.contains(":id") { params.id = m[1] }
        return { matched: true, params }
    }
    if pattern == path { return { matched: true, params: {} } }
    null
}
```

---

## WebSocket Server

```hv
// ws_server.hv
use net

clients = []

server = net.ws.listen(8081, fn(conn) {
    print("Client connected")
    clients.push(conn)
    
    conn.onMessage(fn(msg) {
        print("Received: {msg}")
        // Broadcast to all
        for c in clients {
            if c != conn { c.send(msg) }
        }
    })
    
    conn.onClose(fn() {
        print("Client disconnected")
        clients = clients.filter(c => c != conn)
    })
})

print("WebSocket server on ws://localhost:8081")
```

### WebSocket Client (HTML)

```html
<!DOCTYPE html>
<html>
<body>
    <input id="msg" placeholder="Message">
    <button onclick="send()">Send</button>
    <div id="log"></div>
    <script>
        const ws = new WebSocket("ws://localhost:8081");
        ws.onmessage = e => log.innerHTML += "<div>" + e.data + "</div>";
        function send() { ws.send(msg.value); msg.value = ""; }
    </script>
</body>
</html>
```

---

## Static File Server

```hv
// static_server.hv
use fs
use http
use net

fn serve_static(req) {
    path = req.path == "/" ? "/index.html" : req.path
    full_path = "./public" + path
    
    if !fs.exists(full_path) || fs.isdir(full_path) {
        return { status: 404, body: "Not Found" }
    }
    
    content = fs.read(full_path)
    mime = mime_type(path)
    
    { status: 200, body: content, headers: { "Content-Type": mime } }
}

fn mime_type(path) {
    ext = fs.ext(path)
    match ext {
        "html" => "text/html"
        "css" => "text/css"
        "js" => "application/javascript"
        "json" => "application/json"
        "png" => "image/png"
        "jpg" | "jpeg" => "image/jpeg"
        "svg" => "image/svg+xml"
        "ico" => "image/x-icon"
        _ => "application/octet-stream"
    }
}

server = net.tcp.listen(8080, fn(conn) {
    req = http.parseRequest(conn.recv(4096))
    resp = serve_static(req)
    conn.send(http.formatResponse(resp))
    conn.close()
})

print("Static server on http://localhost:8080")
```

Directory structure:
```
public/
  index.html
  style.css
  app.js
  images/
    logo.png
```

---

## Async Request Handling

```hv
// async_server.hv
use net
use async

// Handle requests concurrently with bounded parallelism
fn handle_concurrently(connections, handler, concurrency = 10) {
    semaphore = async.rateLimit(concurrency)
    
    async.parallelForEach(connections, fn(conn) {
        semaphore.acquire()
        try {
            req = http.parseRequest(conn.recv(4096))
            resp = handler(req)
            conn.send(http.formatResponse(resp))
        } catch e {
            conn.send(http.formatResponse({ status: 500, body: "Error" }))
        } finally {
            conn.close()
            semaphore.release()
        }
    }, concurrency)
}

// Usage
server = net.tcp.listen(8080, fn(conn) {
    // Queue connection for async handling
    handle_concurrently([conn], dispatch)
})
```

---

## TLS/SSL (Production)

```hv
// tls_server.hv
use net

// Requires certificates
cert = fs.read("cert.pem")
key = fs.read("key.pem")

server = net.tcp.listenTLS(8443, cert, key, fn(conn) {
    req = http.parseRequest(conn.recv(4096))
    resp = handle_request(req)
    conn.send(http.formatResponse(resp))
    conn.close()
})

print("HTTPS server on https://localhost:8443")
```

---

## Complete Example: REST API with SQLite

```hv
// api.hv
use http
use net
use sqlite
use json
use async

// Database
db = sqlite.open("app.db")
db.exec("CREATE TABLE IF NOT EXISTS users (id INTEGER PRIMARY KEY, name TEXT, email TEXT)")

// Handlers
fn get_users(req) {
    rows = db.query("SELECT * FROM users")
    { status: 200, body: json.encode(rows), headers: { "Content-Type": "application/json" } }
}

fn create_user(req) {
    user = json.decode(req.body)
    db.exec("INSERT INTO users (name, email) VALUES (?, ?)", user.name, user.email)
    id = db.lastInsertRowid()
    { status: 201, body: json.encode({ id, ...user }), headers: { "Content-Type": "application/json" } }
}

fn get_user(req) {
    row = db.query("SELECT * FROM users WHERE id = ?", req.params.id).first()
    if row { { status: 200, body: json.encode(row), headers: { "Content-Type": "application/json" } } }
    else { { status: 404, body: "Not Found" } }
}

// Routes
routes = {
    "GET": { "/users": get_users, "/users/:id": get_user },
    "POST": { "/users": create_user }
}

// Server
server = net.tcp.listen(8080, fn(conn) {
    req = http.parseRequest(conn.recv(4096))
    method_routes = routes[req.method] || {}
    
    for pattern, handler in method_routes {
        match = match_route(pattern, req.path)
        if match {
            req.params = match.params
            resp = handler(req)
            conn.send(http.formatResponse(resp))
            conn.close()
            return
        }
    }
    conn.send(http.formatResponse({ status: 404, body: "Not Found" }))
    conn.close()
})

print("REST API on http://localhost:8080")
```

---

**Previous:** [Desktop Automation](/guides/desktop-automation)
**Next:** [Using FFI to Call C Libraries →](/guides/ffi)