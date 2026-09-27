---
title: "Concurrency"
description: "Goroutines, channels, fibers, OS threads, coroutines, await, and async utilities."
---

# Concurrency

Havel uses a **hybrid concurrency model**:
- **Goroutines + Fibers**: Cooperative multitasking on a single VM thread
- **OS Threads**: True parallelism with actor-style message passing
- **Channels**: CSP-style synchronization
- **Coroutines**: Stackful coroutines with yield/resume
- **Async Utilities**: Higher-level patterns (debounce, throttle, retry, etc.)

Only one fiber runs at a time. The scheduler time-slices goroutines with instruction budgets.

---

## Goroutines

### Spawning

```hv
go doWork()           // spawn with function
go {                  // spawn with block
    process(data)
}
```

Returns a goroutine ID (thread reference).

### Scheduler

Three priority queues:

| Queue | Priority | Use |
|-------|----------|-----|
| Hotkey | Highest | Hotkey callback goroutines |
| Normal | FIFO | Regular goroutines |
| Background | Lowest | Background tasks |

### States

```
Created → Runnable → Running → Suspended → Runnable → ...
                              \→ Done
```

| State | Description |
|-------|-------------|
| Created | Just spawned, not yet queued |
| Runnable | In scheduler queue |
| Running | Currently executing |
| Suspended | Parked (channel, timer, sleep, hotkey) |
| Done | Execution complete |

### Suspension Reasons

| Reason | Trigger |
|--------|---------|
| `ChannelWait` | `receive()` on empty channel |
| `ChannelSendWait` | `send()` to full channel |
| `ThreadWait` | `wait threadRef` |
| `SleepWait` | `sleep(ms)` |
| `TimerWait` | `wait timerRef` |
| `HotkeyWait` | Persistent hotkey goroutine parked |
| `CoroutineWait` | Awaiting coroutine |

---

## Channels

### Creation

```hv
ch = channel()        // unbuffered (there is no capacity-argument form)
```

### Operations

```hv
ch.send(value)        // send (blocks if full)
val = ch.receive()    // receive (blocks if empty)
ch.close()            // close channel
```

There is no `<-` operator syntax — channels use `send`/`receive`/`close`
prototype methods.

### Blocking Semantics

- **Send**: Parks fiber if buffer full. Unparked when receiver consumes.
- **Receive**: Parks fiber if buffer empty. Unparked when sender provides.
- **Edge-triggered**: Events unpark waiting goroutines immediately.

---

## OS Threads

Real OS threads with actor-style message passing.

```hv
t = thread {
    loop {
        msg = receive()
        process(msg)
    }
}

t.send("hello")       // send message to thread
msg = receive()       // receive in thread (valid inside a thread block)
wait t                // block until thread completes
```

### Operations

| Syntax | Opcode | Description |
|--------|--------|-------------|
| `thread { }` | `THREAD_SPAWN` | Spawn OS thread |
| `wait t` | `THREAD_JOIN` | Block until thread done |
| `t.send(msg)` | `THREAD_SEND` | Send to thread |
| `receive()` | `THREAD_RECEIVE` | Receive next message (inside thread) |

Timers use separate OS threads with `cv.wait_for` for precise timing.

---

## Coroutines

Stackful coroutines with yield/resume.

```hv
co fn generator() {
    for i in 0..10 {
        yield i
    }
}

gen = generator()
while true {
    v = <- gen          // resume, get yielded value
    if v == nil { break }
    print(v)
}
```

### Operations

| Opcode | Description |
|--------|-------------|
| `YIELD_RESUME` | Yield a value or resume a coroutine |

All values inside coroutines are GC-marked to prevent premature collection.

---

## Await / Fiber Receive (`<-`)

The `<-` operator is the generic await mechanism — **prefix** form:

```hv
v = <- channelRef      // await channel receive
v = <- threadRef       // await thread completion
v = <- timerRef        // await timer fire
v = <- waitgroupRef    // await waitgroup done
v = <- coroutineRef    // await coroutine yield
```

Bare `await expr` requires `use async_mod` (it is a module helper
function, not a keyword).

### Dispatch Table

| Target Type | Action |
|-------------|--------|
| WaitGroup | Wait on atomic counter |
| Thread | Suspend on thread wait map |
| Timer | Park with `TIMER_WAIT` |
| Channel | Park with `CHANNEL_RECV` |
| Coroutine | Resume coroutine |

Non-blocking — the fiber parks and returns control to the scheduler.

---

## WaitGroups

```hv
wg = waitgroup()
wg.add(1)

go { work(); wg.done() }

wait wg    // blocks until counter reaches 0
```

Uses `std::atomic<int>` — thread-safe without locking.

---

## Select

Not implemented. There is no `select` statement; use a merged channel,
`<-`, or the `async_mod` helpers (`race`, `allSettled`, `chan`) for
multiplexing.

---

## Async Utilities (Sidecar Module)

Higher-level patterns implemented in Havel (`modules/app/async_mod.hv`,
loaded with `use async_mod`; plain `use async` only exposes a trivial
`await` shim):

```hv
use async_mod

// Timing
debounced = async_mod.debounce(fn(msg) { print(msg) }, 100)
throttled = async_mod.throttle(fn(x) { print(x) }, 500)
result = async_mod.retry(fn() { http.get(url) }, 3, 100)
result = async_mod.withTimeout(fn() { slow() }, 5000)
winner = async_mod.race([fn() { slow() }, fn() { fast() }])
cached = async_mod.once(fn() { expensive() })

// Parallel
results = async_mod.parallelMap(items, fn(x) { process(x) }, 4)
results = async_mod.parallelFilter(items, fn(x) { check(x) }, 4)
async_mod.parallelForEach(items, fn(x) { sideEffect(x) }, 4)

// Promises (channel-based)
p = async_mod.promise(fn() { compute() })
async_mod.then(p, fn(v) { print(v) }, fn(e) { print("err: ${e}") })
all = async_mod.all([p1, p2, p3])
settled = async_mod.allSettled([p1, p2, p3])

// Channels
ch = async_mod.chan(10)
first = async_mod.chanSelect([ch1, ch2])
merged = async_mod.merge([ch1, ch2])
async_mod.fanOut(source, workers)

// Resilience
wg = async_mod.waitgroup()
limiter = async_mod.rateLimit(10)      // 10/sec max
breaker = async_mod.circuitBreaker(fn() { risky() }, 5, 30000)
```

---

**Previous:** [Types](/language/types)
**Next:** [Modules →](/language/modules)