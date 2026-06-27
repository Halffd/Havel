# Concurrency

Havel's concurrency model: cooperative goroutines on a single VM thread, real OS threads for parallelism, and CSP-style channels for communication.

---

## Model Overview

Havel uses a **hybrid concurrency model**:

- **Goroutines + Fibers**: Cooperative multitasking on a single VM thread (Go-style syntax, single-threaded execution)
- **OS Threads**: True parallelism with actor-style message passing
- **Channels**: CSP-style synchronization between goroutines and threads
- **Coroutines**: Stackful coroutines with yield/resume

Only one fiber runs at a time. The scheduler time-slices goroutines with instruction budgets.

---

## Goroutines

### Spawning

```
go doWork()           // spawn goroutine with function
go {                  // spawn goroutine with block
    process(data)
}
```

Compiled to: `LOAD_GLOBAL "thread_spawn"` + `CALL` + `POP`

### Scheduler

**Source**: `src/havel-lang/compiler/vm/Scheduler.hpp`

The `Scheduler` is a singleton (`Scheduler::instance()`) with three priority queues:

| Queue | Priority | Use |
|-------|----------|-----|
| Hotkey | Highest (prepend) | Hotkey callback goroutines |
| Normal | FIFO | Regular goroutines |
| Background | Lowest | Background tasks |

### Goroutine States

```
Created → Runnable → Running → Suspended → Done
```

| State | Description |
|-------|-------------|
| Created | Just spawned, not yet queued |
| Runnable | In scheduler queue, waiting to run |
| Running | Currently executing |
| Suspended | Parked (waiting on channel, timer, thread, sleep, hotkey) |
| Done | Execution complete |

### Suspension Reasons

| Reason | Trigger |
|--------|---------|
| `None` | Not suspended |
| `ChannelWait` | `receive()` on empty channel |
| `ChannelSendWait` | `send()` to full channel |
| `ThreadWait` | `wait threadRef` |
| `SleepWait` | `sleep(ms)` |
| `TimerWait` | `wait timerRef` / interval await |
| `HotkeyWait` | Persistent hotkey goroutine parked between triggers |
| `CoroutineWait` | Coroutines await |

---

## Fibers

**Source**: `src/havel-lang/compiler/vm/Fiber.hpp`

Each goroutine owns a `Fiber` — the execution context:

```
Fiber:
  - FiberStack: Value vector + stack pointer
  - locals: local variable map
  - call_frames: CallFrame vector
  - state: RUNNABLE | RUNNING | SUSPENDED | DEAD | YIELDED | ERROR
  - suspension_reason: why the fiber paused
```

### Fiber States

| State | Meaning |
|-------|---------|
| `CREATED` | Spawned but not yet scheduled |
| `RUNNABLE` | Ready to execute |
| `RUNNING` | Currently executing |
| `SUSPENDED` | Paused (channel, timer, thread join, sleep, hotkey) |
| `DONE` | Execution complete |

### SuspensionReason

| Reason | Description |
|--------|-------------|
| `NONE` | Not suspended |
| `YIELD` | Explicit `yield` |
| `CHANNEL_RECV` | Waiting to receive from channel |
| `CHANNEL_SEND` | Waiting to send to full channel |
| `THREAD_JOIN` | Waiting for thread to complete |
| `TIMER` | Waiting for timer to fire |
| `SLEEP` | Sleeping for duration |
| `EXTERNAL` | External system parked this fiber |
| `HOTKEY_WAIT` | Parked waiting for hotkey trigger |
| `AWAIT` | Generic await |
| `COROUTINE_WAIT` | Waiting for coroutine |

### Fiber Swap

The VM swaps fiber state via `loadFiberState()` / `saveFiberState()` (VM.cpp:602+). This copies the entire execution context (stack, locals, frames, IP) between the VM and the Fiber object.

`suspend()` requires `state == RUNNING` — calling it on a non-running fiber throws.

---

## ExecutionEngine

**Source**: `src/havel-lang/compiler/vm/ExecutionEngine.hpp`

The `ExecutionEngine` is the central event loop coordinator. Its `executeFrame()` algorithm:

1. **Drain EventQueue callbacks** (`processAll`) — handles events from non-VM threads
2. **GC safe point** + finalizer drain
3. **Drain deferred callbacks** from non-VM threads (`deferToVM`)
4. **Wake sleeping goroutines** with expired deadlines
5. **Pick next runnable goroutine** via `Scheduler::pickNext()` (hotkey → normal → background)
6. **Load fiber state** → execute instruction budget in tight loop → **save fiber state**
7. **Handle result**:
   - `YIELD` → requeue goroutine
   - `SUSPENDED` → park goroutine
   - `RETURNED` → mark Done
   - `ERROR` → mark Done

### Time Slicing

Each goroutine gets `max_instructions_per_tick` instructions before yielding. This prevents any single goroutine from monopolizing the VM thread.

### Event Handlers

| Event | Handler | Effect |
|-------|---------|--------|
| Thread complete | `onThreadComplete` | Find goroutine by `WaitTarget(THREAD_JOIN)`, store result, unpark |
| Timer fire | `onTimerFire` | Find by `WaitTarget(TIMER_WAIT)`, store result, unpark |
| Channel receive | `onChannelRecv` | Find by `WaitTarget(CHANNEL_RECV)`, unpark |
| Channel send | `onChannelSend` | Find by `WaitTarget(CHANNEL_SEND)`, unpark |
| Variable change | `onVariableChanged` | Evaluate watcher conditions, resume fired fibers |

### Persistent Hotkey Goroutines

Hotkey callback goroutines are **persistent** — after executing, they re-suspend instead of marking Done, and are recycled via `requeueFront()` for immediate re-execution on the next hotkey press.

---

## Channels

### Creation

```
ch = channel()        // unbuffered
ch = channel(10)      // buffered with capacity 10
```

Compiled to: `LOAD_GLOBAL "channel.new"` + `CALL`

### Operations

```
ch <- value           // send
val = <- ch           // receive
```

| Opcode | Description |
|--------|-------------|
| `CHANNEL_NEW` | Create channel |
| `CHANNEL_SEND` | Send value to channel |
| `CHANNEL_RECEIVE` | Receive value from channel |
| `CHANNEL_CLOSE` | Close channel |

### Blocking Semantics

- **Send**: If channel buffer is full, the fiber parks with `CHANNEL_SEND` reason. When a receiver consumes a value, `onChannelSend` unparks the sender.
- **Receive**: If channel buffer is empty, the fiber parks with `CHANNEL_RECV` reason. When a sender provides a value, `onChannelRecv` unparks the receiver.
- Channels are **edge-triggered**: events unpark waiting goroutines immediately.

### Storage

Channels are stored in `GCHeap` as `std::unordered_map<uint32_t, std::vector<Value>>`. The `ConcurrencyBridge` manages channel lifecycle and routes events through the `EventQueue` for cross-thread signaling.

---

## OS Threads

**Source**: `src/havel-lang/compiler/vm/Thread.hpp`

Real OS threads with actor-style message passing:

```cpp
class Thread {
    std::thread thread_;
    std::queue<Message> message_queue_;
    std::mutex queue_mutex_;
    std::condition_variable queue_cv_;
    std::atomic<bool> running_, paused_, stopped_;
};
```

Messages are `variant<string, int, double>`.

### Spawning

```
t = thread {
    // runs in a real OS thread
    loop {
        msg = receive()
        process(msg)
    }
}
```

### Operations

| Syntax | Opcode | Description |
|--------|--------|-------------|
| `thread { }` | `THREAD_SPAWN` | Spawn OS thread |
| `wait t` | `THREAD_JOIN` | Block until thread completes |
| `t <- msg` | `THREAD_SEND` | Send message to thread |
| `receive()` | `THREAD_RECEIVE` | Receive next message |

### Interval/Timer Threads

Timers use separate OS threads with `cv.wait_for` for precise timing. These are managed by the `ConcurrencyBridge`.

---

## Coroutines

### Storage

Coroutines are stored in `GCHeap` as `std::unordered_map<uint32_t, CoroutineData>` with:

- `stack`: saved value stack
- `locals`: saved local variables
- `caller_stack`: caller's state for resume
- `yield_values`: values passed via yield

### Operations

| Opcode | Description |
|--------|-------------|
| `YIELD_RESUME` | Yield a value or resume a coroutine |

Yield pushes a value and suspends. Resume restores the coroutine's state and continues execution.

### GC Marking

All values inside coroutines are marked during GC (GC.cpp:765-780) to prevent premature collection of referenced objects.

---

## WaitGroups

### Operations

| Syntax | Opcode | Description |
|--------|--------|-------------|
| `waitgroup()` | `WAITGROUP_NEW` | Create waitgroup |
| `wg.add(n)` | `WAITGROUP_ADD` | Add to counter |
| `wg.done()` | `WAITGROUP_DONE` | Decrement counter |
| `wait wg` | `THREAD_JOIN` | Block until counter reaches 0 |

`THREAD_JOIN` dispatches on WaitGroup vs Thread at runtime.

WaitGroups use `std::atomic<int>` for the counter — thread-safe without locking.

---

## FIBER_AWAIT (`<- expr`)

The `<-` operator is the generic await mechanism:

```
result <- channelRef     // await channel receive
result <- threadRef      // await thread completion
result <- timerRef       // await timer fire
result <- waitgroupRef   // await waitgroup done
```

`FIBER_AWAIT` pops the target and dispatches on type:

| Target Type | Action |
|-------------|--------|
| WaitGroup | Wait on atomic counter |
| Thread | Suspend on `thread_wait_map_` |
| Timer | Park with `TIMER_WAIT` |
| Channel | Park with `CHANNEL_RECV` |

This is **non-blocking** — the fiber parks and returns control to the scheduler.

---

## Concurrency Opcodes Summary

| Opcode | Description |
|--------|-------------|
| `THREAD_SPAWN` | Create OS thread |
| `THREAD_JOIN` | Wait for thread or waitgroup |
| `THREAD_SEND` | Send message to thread |
| `THREAD_RECEIVE` | Receive message from thread |
| `YIELD_RESUME` | Yield/resume coroutine |
| `GO_ASYNC` | Spawn goroutine |
| `FIBER_AWAIT` | Generic await |
| `FIBER_SLEEP` | Sleep fiber |
| `CHANNEL_NEW` | Create channel |
| `CHANNEL_SEND` | Send to channel |
| `CHANNEL_RECEIVE` | Receive from channel |
| `CHANNEL_CLOSE` | Close channel |
| `WAITGROUP_NEW` | Create waitgroup |
| `WAITGROUP_ADD` | Add to waitgroup |
| `WAITGROUP_DONE` | Decrement waitgroup |

---

## Thread Safety

### VM

| Resource | Mechanism |
|----------|-----------|
| `globals` | `std::shared_mutex globals_mutex_` |
| `thread_wait_map_` | `std::shared_mutex thread_wait_mutex_` |
| Global get/set | `setGlobalThreadSafe()` / `getGlobalThreadSafe()` use shared_mutex |

### GC

| Resource | Mechanism |
|----------|-----------|
| `ObjectEntry::shape_version` | `std::atomic<uint64_t>` |
| `subHeapBytes` | CAS (compare-and-swap) |
| `next_enum_id_` | CAS |
| `GC::mutex_` | `mutable` mutex for const method locking |

No stop-the-world GC. Mark-and-sweep is single-threaded with the VM.

### Scheduler

| Resource | Mechanism |
|----------|-----------|
| Queue operations | `std::mutex scheduler_mutex_` |
| Deferred wakeups | `eventfd` for cross-thread signaling |

### ConcurrencyBridge

| Resource | Mechanism |
|----------|-----------|
| Thread pool | `std::mutex threads_mutex_` |
| Timer tracking | `std::mutex timers_mutex_` |
| Event queue | `std::mutex queue_mutex_` + condition variable |

### Thread

| Resource | Mechanism |
|----------|-----------|
| Message queue | `std::mutex` + `std::condition_variable` |
| Running/Paused/Stopped flags | `std::atomic<bool>` |

---

## Reactive Watchers

**Source**: `src/havel-lang/compiler/vm/WatcherRegistry.hpp`, `DependencyTracker.hpp`

Edge-triggered `when` statements watch variable changes:

1. A `when` condition is compiled as a bytecode snippet
2. `DependencyTracker` records which globals the condition accesses during evaluation
3. When a global is modified (`onVariableChanged`), the tracker evaluates all dependent conditions
4. If a condition transitions from `false` to `true`, the corresponding fiber is resumed

This enables reactive patterns:

```
when temperature > 30 {
    startCooling()
}
```
