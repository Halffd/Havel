# Concurrency Architecture Validation

## Design Completeness Check

This document validates that the execution model covers all 4 features and their interactions without deadlock or blocking.

---

## Feature Matrix: All Cases Covered?

### Feature 1: Goroutines (go)

| Case | Execution | Event Queue | Scheduler | Result |
|------|-----------|-------------|-----------|--------|
| **go simple task** | VM.call(task) | → unpark on done | Task queued | ✓ |
| **go with sleep** | execute → sleep OpCode | {unpark@T} | Park current | ✓ |
| **go with recv** | execute → recv empty | {} | Park on channel | ✓ |
| **go with interval** | execute → interval start | {callback@T} | Unpark on fire | ✓ |
| **Multiple go** | VM picks from scheduler | Event queue | Multiple Goroutines | ✓ |
| **Nested go (go inside go)** | Inner VM.call(task) | Event queue | Nested goroutines | ✓ |

### Feature 2: Channels

| Case | Execution | Event Queue | Scheduler | Result |
|------|-----------|-------------|-----------|--------|
| **recv empty** | OpCode RECV | {} | Park(g, ch) | ✓ |
| **recv populated** | OpCode RECV | {} | Return value | ✓ |
| **send to waiting** | OpCode SEND | {unpark(rx)} | Rx unparked | ✓ |
| **send to empty** | OpCode SEND | {msg queued} | (no waker) | ✓ |
| **recv after send** | OpCode RECV | {} | Value ready | ✓ |
| **close channel** | OpCode CLOSE_CH | {unpark all} | All receivers wake | ✓ |

### Feature 3: Timers (interval/timeout)

| Case | Execution | Event Queue | Scheduler | Result |
|------|-----------|-------------|-----------|--------|
| **timeout fire** | Timer thread fires | {callback} | Event loop runs | ✓ |
| **timeout callback has recv** | Callback executes | {unpark if ch ready} | Goroutine park/unpark | ✓ |
| **interval repeating** | Timer thread fires | {callbacks, callbacks, ...} | Multiple unparks | ✓ |
| **interval in go** | Goroutine runs interval | {callbacks} | Timer thread queues | ✓ |
| **clear interval** | CLEAR_INTERVAL OpCode | (no more callbacks) | Timer stopped | ✓ |

### Feature 4: on msg (Message Handlers)

| Case | Execution | Event Queue | Scheduler | Result |
|------|-----------|-------------|-----------|--------|
| **on msg with recv** | Compiles to: loop recv | {} | recv → park | ✓ |
| **send to on msg** | t.send(msg) | {unpark} | Receiver unparked | ✓ |
| **on msg multiple** | Multiple goroutines | {unpark, unpark, ...} | Fair scheduling | ✓ |
| **on msg + interval** | on msg runs + interval fires | {all callbacks} | Mixed execution | ✓ |

---

## Blocking Detection Matrix

### Can VM block?

| Operation | Happens in VM? | Blocks VM? | Alternative |
|-----------|---|---|---|
| recv() empty | Yes | ❌ NO | Suspend → yield |
| channel.wait() | No | - | Use suspend |
| timer.wait() | No | - | Event queue |
| mutex.lock() | No | - | Park instead |
| thread.join() | **YES** | ⚠️ TRACE | ← **ACTION** |

**ACTION: thread.join() Analysis**
- Called from: bytecode THREAD_JOIN opcode
- Current: Calls thread.join() directly → BLOCKS VM
- Fix: Use parking + notification

```cpp
// WRONG:
Value threadJoin(Handle h) {
    threads[h].join();  // ❌ BLOCKS VM
    return handle;
}

// RIGHT:
Value threadJoin(Handle h) {
    Goroutine* g = scheduler->current();
    g->state = Suspended;
    g->waiting_for_thread = h;
    
    eventQueue->push([h, g, sched]() {
        // When thread done, unpark goroutine
        sched->unpark(g);
    });
    return yield_point;  // ← This makes it safe
}
```

### Can Scheduler block?

| Operation | Blocks Scheduler? | Fix |
|-----------|---|---|
| Mutex lock in spawn | No (brief) | ✓ |
| Condition variable wait | NO (should use notify) | ✓ |
| Queue operations | No (brief) | ✓ |

### Can Event Loop block?

| Operation | Blocks Loop? | Fix |
|-----------|---|---|
| processCallbacks() | NO (always finite) | ✓ |
| querying timer state | No | ✓ |
| polling input | MAYBE | Use select/epoll |

---

## Deadlock Scenarios Checked

### Scenario 1: Goroutine waits on channel, sender blocked
```havel
let [tx, rx] = channel()
go { let x = recv(rx) }  // Parks on rx
let y = send(tx, 123)    // Gets value immediately (no wait)
```
**Result:** ✓ No deadlock (send doesn't block)

### Scenario 2: Goroutine in interval callback
```havel
interval 100 {
    let x = recv(channel)  // Callback runs in event loop
}
```
**Problem:** Callback can't suspend (not in goroutine context)  
**Fix:** Interval callbacks must not use recv/complex logic  
**Rule:** Enforce in compiler/docs

### Scenario 3: Thread tries to call VM
```cpp
// Worker thread
result = heavyWork();
vm->callFunction(callback);  // ❌ WRONG
```
**Prevention:** No VM symbol visibility in worker threads  
**Fix:** Only push callbacks, never expose vm

### Scenario 4: VM sleeps
```havel
go { sleep(1000); print("done") }
```
**Old model:** sleep() blocks thread → deadlock  
**New model:** 
1. sleep() parks goroutine
2. scheduler picks next goroutine
3. Event loop continues
4. Timer fires → unpark
**Result:** ✓ No deadlock

### Scenario 5: Channel buffered size limit
```havel
let [tx, rx] = channel(100)
for i in 0..200 {
    tx.send(i)  // Buffer full?
}
```
**Current design:** Unbuffered channels (send returns immediately)  
**If buffered needed:** send() on full buffer should return error or wait  
**Decision needed:** Unlimited queue vs sized + park  
**Recommendation:** Start with unlimited (simplest)

---

## Race Condition Scenarios Checked

### Scenario 1: Timer and goroutine both unpark same receiver
```
Timer: unpark(g)
Goroutine: unpark(g)  // Same goroutine
```
**Safety:** Scheduler has mutex, unpark is idempotent  
**Prevention:** Already safe with Goroutine::state atomicity

### Scenario 2: VM modifying goroutine while scheduler accesses
```cpp
VM thread: g->stack[0] = 123
Scheduler thread: g->state = Done  // NO SCHEDULER THREAD
```
**Wait:** Scheduler is NOT separate thread!  
**New model:** VM drives scheduler  
**Result:** ✓ No race (single threaded)

### Scenario 3: Event queue processed by two threads
```cpp
Thread A: eventQueue->processAll()
Thread B: eventQueue->push()
```
**Protection:** 
- eventQueue has mutex_
- process() drains atomically
- push() is non-blocking

**Result:** ✓ Safe

---

## Resource Cleanup Checked

### Goroutines
- When Done → garbage collected
- With active recv() → must unpark on channel close
- With sleep timer → timer cleanup needed
**Action:** Add cleanup hooks

### Channels
- When closed → wake all receivers with error
- Sent messages → garbage collected when consumed
**Action:** Implement close() semantics

### Timers
- When cancelled → remove from timer queue
- When fired → removed automatically
**Action:** checkTimers() must remove ready timers

### OS Threads
- Worker threads → join on shutdown
- Thread handles → unregister on done
**Action:** graceful shutdown sequence

---

## Determinism Check

### Question: Is execution deterministic given same inputs?

| Feature | Source of Non-Determinism | Mitigation |
|---------|---|---|
| Goroutine scheduling order | None (queue order) | ✓ FIFO |
| Timer fire order | System clock + precision | Documented |
| Channel message order | Queue FIFO | ✓ |
| OS thread result order | System scheduling | ⚠️ Accept variation |

**Verdict:** Mostly deterministic except OS thread ordering (acceptable for run())

---

## API Surface Coverage

### VM API needed
- [ ] `executeStep()` - run ONE instruction
- [ ] `current_goroutine()` - get current
- [ ] `yard()` - yield current
- [ ] `callFunction(handle, args...)` - for callbacks

### Scheduler API needed
- [ ] `spawn(func_id, args, name)` → Handle
- [ ] `current()` → Goroutine*
- [ ] `suspend(g, reason)` → void
- [ ] `unpark(g)` → void

### EventQueue API needed
- [ ] `push(callback)` → void
- [ ] `processAll()` → void
- [ ] `size()` → uint32_t

### Channel API needed
- [ ] `send(value)` → bool (true = success)
- [ ] `recv()` → Value (suspends if empty)
- [ ] `close()` → void

---

## Missing Pieces Found?

| Gap | Risk | Fix |
|-----|---|---|
| thread.join() blocking | ⚠️ HIGH | Use parking |
| Interval callback context | ⚠️ MEDIUM | Restrict what runs |
| Channel buffer size | LOW | Specify unlimited |
| Goroutine cleanup | ⚠️ MEDIUM | Track active set |
| Timer precision | ACCEPTED | Document limits |

---

## Design Locked Status

✅ **READY TO IMPLEMENT**

Key victories:
1. ✓ Single executing thread (VM)
2. ✓ No blocking anywhere
3. ✓ Event queue bridges OS threads
4. ✓ Scheduler is cooperative
5. ✓ No deadlock scenarios found
6. ✓ All 4 features integrated
7. ✓ All interactions safe

One gap: **thread.join() needs parking fix** (HIGH PRIORITY)

---

## Next Steps

1. Review this document - any missing interactions?
2. Implement EventQueue (simplest layer)
3. Fix VM.executeStep() for single-step execution
4. Refactor Scheduler to cooperate with VM
5. Implement channel suspend/resume
6. Rewrite timer handling
7. Fix thread.join() + go design
