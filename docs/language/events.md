# Events

`on` is a first-class event subscription primitive over a generic event
bus. Producers publish; the runtime dispatches in the VM's pump loop;
handlers execute in VM context. The language sees no inotify, X11, or
WM mechanics — those are event sources at the host/platform boundary.

## Subscribing

```hv
on "my.event" {
  print("handler fired")
}

on file.changed("/tmp/config.hv") {
  config.reload()
}

on file.created("./plugins/*.hv") {
  plugins.load(event.path)
}

on window.focused {
  print("focused: " + event.window)
}
```

Dotted names assemble from tokens (`file.changed` = `file` `.` `changed`)
— the runtime assigns no meaning to them. String forms work the same.

## Payload: the `event` variable

The handler receives one argument named `event`: an object of the
published payload's fields (string-typed).

```hv
on "payload.event" {
  print("got " + event.foo)     // event.foo from the publish below
}
event.publish("payload.event", {foo: 42})
```

An event published with no payload passes `null` as `event`.

## Filters

A `where` predicate gates per-event; the body only runs when it matches.
The predicate sees the `event` variable plus the enclosing scope.

```hv
on window.focused where event.window == "12345" {
  print("firefox focused")
}
```

## Emitting

`emit` is syntax sugar over `event.publish`. Libraries build their own
event APIs without touching the compiler:

```hv
emit "mpv.playback_started"
emit "my.event" {foo: 123}
```

Dispatch is asynchronous: publish queues the event, the VM's pump runs
matching handlers on the next loop iteration. Matching handlers are
snapshotted at publish time — a later `event.cancel` never retroactively
drops an event that was published while the subscription existed.

## Subscription lifetime

The subscription id is the `on` expression's value:

```hv
w = on "sub.event" {
  fired = fired + 1
}
event.publish("sub.event")   // handler fires
event.cancel(w)              // stop receiving
event.publish("sub.event")   // handler does not fire
```

`event.subscribe(name, handler, [arg])` returns the same id when called
as a host function; `event.unsubscribe(id)` is an alias for
`event.cancel(id)`. Scripts also see `event.subscriptionArg(id)` — the
optional arg from the subscription (a watch path for `on file.changed(path)`).

## File watching

`on file.*` subscriptions start the inotify watcher automatically; the
subscription's arg (the path) is what gets watched. Globs split into
(directory, basename pattern) — the directory is the watch, the pattern
filters with fnmatch.

- `file.created(path)` / `file.modified(path)` / `file.deleted(path)` —
  kind-specific names
- `file.changed(path)` — the umbrella (any of the above)

Payload fields: `path` (the changed file — for globs: directory + name),
`kind` (created/modified/deleted).

Coalescing: a 50ms settle window collapses the editor-save cascade
(OPEN/MODIFY/MODIFY/CLOSE_WRITE) into one event per save, so
`on file.changed` runs once per save.

## Window events

`on window.*` subscriptions start the X11 window event source (own
connection; SubstructureNotify + FocusChange on the root). Available
names:

| name | payload |
|------|---------|
| `window.created` | `window` |
| `window.hidden` | `window` |
| `window.destroyed` | `window` |
| `window.focused` | `window` |
| `window.unfocused` | `window` |
| `window.moved` | `window`, `x`, `y`, `width`, `height` |
| `window.resized` | same as moved |

Identical consecutive configure events for the same window are skipped.

## Lifecycle hooks

```hv
on start { ... }   // runs once at script load, BEFORE the entry function
on reload { ... }  // runs on every auto-reload, never on first load
```

The hook runs before main, so main's top-level assignments run after it
and clobber shared variables the hook set — communicate via side effects
or the `_G` escape:

```hv
on start {
  _G["hookRan"] = 1
}
if _G["hookRan"] != 1 {
  print("hook did not run")
}
```

## Auto-reload

```hv
app.enableReload()      // start watching the script file (250ms mtime poll)
app.disableReload()     // stop
app.toggleReload()      // flip; returns the new state
app.reload()            // read the current state
```

On change the engine re-compiles the script, swaps the main chunk
(globals persist; old-goroutine references stay alive), clears all
generic event subscriptions (so handlers never outlive their compile
unit), and runs the script's `on reload { }` body — which handles its
own cleanup (e.g. re-registering hotkeys).

The reload runs OUTSIDE any fiber context: a nested `callFunctionSync`
inside a goroutine frame wedges the pipeline.

## Async channels

```hv
async.send("mychan", "data")
x = async.receive("mychan")      // blocking: waits until a message arrives
y = async.tryReceive("mychan")   // non-blocking: "" when empty
```

String-keyed channels shared across the host process (they persist
across script reloads). `receive` blocks the calling VM thread — a bare
receive on an empty channel never returns; cross-thread rendezvous
(hotkey handler -> script) works, cross-goroutine rendezvous within one
interpreter would deadlock.
