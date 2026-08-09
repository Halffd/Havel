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
ch = channel()        // unbuffered
ch = channel(10)      // buffered (capacity 10)
```

### Operations

```hv
ch <- value           // send (blocks if full)
val = <- ch           // receive (blocks if empty)
ch.close()            // close channel
```

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

t <- "hello"          // send message to thread
msg = receive()       // receive in thread
wait t                // block until thread completes
```

### Operations

| Syntax | Opcode | Description |
|--------|--------|-------------|
| `thread { }` | `THREAD_SPAWN` | Spawn OS thread |
| `wait t` | `THREAD_JOIN` | Block until thread done |
| `t <- msg` | `THREAD_SEND` | Send to thread |
| `receive()` | `THREAD_RECEIVE` | Receive next message |

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
    val = <- gen        // resume, get yielded value
    if val == nil { break }
    print(val)
}
```

### Operations

| Opcode | Description |
|--------|-------------|
| `YIELD_RESUME` | Yield a value or resume a coroutine |

All values inside coroutines are GC-marked to prevent premature collection.

---

## Await / Fiber Receive (`<-`)

The `<-` operator is the generic await mechanism:

```hv
result <- channelRef     // await channel receive
result <- threadRef      // await thread completion
result <- timerRef       // await timer fire
result <- waitgroupRef   // await waitgroup done
result <- coroutineRef   // await coroutine yield
```

Also available as `await expr` (equivalent).

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
wg.add(3)

go { work(); wg.done() }
go { work(); wg.done() }
go { work(); wg.done() }

wait wg    // blocks until counter reaches 0
```

Uses `std::atomic<int>` — thread-safe without locking.

---

## Select

Multiplex over multiple channels:

```hv
ch1 = channel()
ch2 = channel()

go { sleep(100); ch1 <- "one" }
go { sleep(200); ch2 <- "two" }

select {
    case val <- ch1: print("ch1: {val}")
    case val <- ch2: print("ch2: {val}")
}
```

---

## Async Utilities (Sidecar Module)

Higher-level patterns implemented in Havel (`modules/app/async.hv`):

```hv
use async

// Timing
debounced = async.debounce(fn(msg) { print(msg) }, 100)
throttled = async.throttle(fn(x) { print(x) }, 500)
result = async.retry(fn() { http.get(url) }, 3, 100)
result = async.withTimeout(fn() { slow() }, 5000)
winner = async.race([fn() { slow() }, fn() { fast() }])
cached = async.once(fn() { expensive() })

// Parallel
results = async.parallelMap(items, fn(x) { process(x) }, 4)
results = async.parallelFilter(items, fn(x) { check(x) }, 4)
async.parallelForEach(items, fn(x) { sideEffect(x) }, 4)

// Promises (channel-based)
p = async.promise(fn() { compute() })
async.then(p, fn(v) { print(v) }, fn(e) { print("err: {e}") })
all = async.all([p1, p2, p3])
settled = async.allSettled([p1, p2, p3])

// Channels
ch = async.chan(10)
first = async.chanSelect([ch1, ch2])
merged = async.merge([ch1, ch2])
async.fanOut(source, workers)

// Resilience
wg = async.waitgroup()
limiter = async.rateLimit(10)      // 10/sec max
breaker = async.circuitBreaker(fn() { risky() }, 5, 30000)
```

---

**Previous:** [Types](/language/types)
**Next:** [Modules →](/language/modules)