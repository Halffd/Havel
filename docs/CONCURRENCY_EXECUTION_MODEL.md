# Havel Concurrency Execution Model (LOCKED)

## Purpose
Single, unified concurrency model across threads, coroutines, channels, and timers.

## Core Principle
**VM Thread = Authority**

Everything else (OS threads, timers, channels) feeds callbacks into a queue that the VM processes.

---

## Architecture Layers

### Layer 0: VM Thread (The Authority)
**Location:** `VM::execute()` main loop  
**Runs:** Bytecode interpreter  
**Frequency:** Once per event loop iteration  
**Responsibility:**
- Execute ONE bytecode instruction
- Switch coroutines on yield
- Never block
- Never spawn threads

```cpp
// Main loop (EventListener)
while (running) {
    vm_->executeStep();          // Layer 0: Run ONE instruction
    scheduler_->processScheduler();  // Check for runnable goroutines
    event_queue_->processCallbacks();  // Timers, channels, thread messages
    input_listener_->pollInput();  // Handle input
}
```

### Layer 1: Scheduler (Goroutine Park/Unpark)
**Location:** `Scheduler`, integrated into VM  
**Manages:** Goroutine queue (no OS threads)  
**Invariant:** Goroutines are bytecode execution contexts, not functions  

```cpp
struct Goroutine {
    uint32_t id;
    uint32_t function_id;        // Which function (bytecode)
    uint32_t ip;                 // Instruction pointer
    std::vector<Value> stack;
    std::vector<Value> locals;
    GoroutineState state;        // Runnable, Suspended, Done
    uint32_t resume_at_time;     // For sleep()
    uint32_t waiting_for_channel; // Which channel if blocked on recv()
};
```

**API:**
- `unpark(g)` → moves to runnable queue
- `suspend(g, reason)` → waits for reason
- `current()` → current goroutine

### Layer 2: Event Queue (Callback Distribution)
**Location:** New `EventQueue` class  
**Holds:** Callbacks from timers, channels, threads  
**Never calls:** VM directly  

```cpp
class EventQueue {
    std::queue<std::function<void()>> callbacks;
    
    void push(std::function<void()> cb);
    void processAll();  // Drain queue
};
```

**Rules:**
- Timer expires → `queue.push([sched, g] { sched->unpark(g); })`
- Channel send → `queue.push([sched, g] { sched->unpark(g); })`
- Thread message → `queue.push([ch] { ch->do_something(); })`

### Layer 3: OS Threads (Isolated Workers)
**When to use:** 
- `run("blocking_command")` → spawn thread
- Heavy computation → thread pool  
- Network I/O → thread  

**Rule:** Communicate only via queue

```cpp
// Worker thread
void workerThread(...) {
    // Do blocking work
    result = doHeavyWork();
    
    // Enqueue callback back to VM
    eventQueue->push([result, vm] {
        vm->setGlobalVariable("last_result", result);
    });
}
```

---

## Language Feature Mapping

### 1. Goroutines (go)
**Syntax:** `go expr`  
**Compiles to:** Scheduler::spawn(bytecode_address)  
**Returns:** Goroutine handle (for .join(), .send())  
**Model:** Coroutine (cooperative, in VM)

```havel
let t = go task("A")
t.join()
```

**Execution:**
1. Parser → GoExpression AST
2. Compiler → emit OpCode::SPAWN_GOROUTINE (addr, args)
3. VM → Scheduler.spawn(addr, args) → returns handle
4. t.join() → suspends current goroutine until t is done
5. Scheduler parks current goroutine
6. When target done, scheduler unparks

### 2. Channels
**Syntax:** `let [tx, rx] = channel()`  
**Model:** Queue with parking  
**Key:** recv() suspends, never blocks VM  

```cpp
// Correct implementation
Value recv(Handle rx) {
    if (queue has data) {
        return queue.pop();
    } else {
        // Suspend current goroutine
        current_goroutine->state = Suspended;
        current_goroutine->waiting_for_channel = rx;
        scheduler->suspend(current_goroutine);
        
        // Control returns to event loop
        // When data arrives, scheduler unparks this goroutine
        return yield_point;  // VM will resume here
    }
}
```

### 3. Timers (interval/timeout)
**Syntax:** `interval ms { ... }` / `timeout ms { ... }`  
**Model:** Event queue callbacks  
**Never:** Spawn threads  

```cpp
// Timer fires
void onTimerExpired(timer_id) {
    Callback cb = timers[timer_id];
    
    eventQueue->push([cb, vm] {
        // This executes in main event loop, NOT timer thread
        vm->callFunction(cb);
    });
}

// For `on msg` handlers
eventQueue->push([receiver_goroutine, msg, sched] {
    receiver_goroutine->input_message = msg;
    sched->unpark(receiver_goroutine);
});
```

### 4. Message Passing (on msg)
**Syntax:** 
```havel
fn receiver() {
    on msg {
        print(msg)
    }
}

let t = go receiver()
t.send("hello")
```

**Compiles to:**
```havel
fn receiver() {
    loop {
        let msg = recv(this_thread_channel);
        print(msg)
    }
}
```

**Execution:**
1. recv() on empty channel → try to suspend
2. send() → enqueue message, unpark receiver goroutine
3. Event loop processes unpark → scheduler moves to runnable
4. Next VM.step() picks receiver goroutine
5. recv() now has data → returns it

### 5. Sleeping (sleep/wait)
**Syntax:** `sleep(ms)`  
**Model:** Timer + goroutine parking  

```cpp
Value builtinSleep(uint32_t ms) {
    Goroutine* g = scheduler->current();
    
    g->resume_at_time = now() + ms;
    g->state = Suspended;  // Park it
    
    eventQueue->push([g, sched] {
        sched->unpark(g);
    });
    // This callback executes after timer fires
    
    return null;
}
```

---

## Execution Timeline Example

**Code:**
```havel
go { print("A"); sleep(100) }
go { print("B") }
```

**Timeline:**
```
Time │ VM Step         │ Scheduler      │ Event Queue    │ Output
─────┼─────────────────┼────────────────┼────────────────┼───────
0    │ print("A")      │ G1 Running     │ empty          │ A
1    │ sleep(100)      │ G1→Suspended   │ {unpark@100ms} │
1    │ pick next       │ G2 Running     │ {unpark@100ms} │
2    │ print("B")      │ G2 Running     │ {unpark@100ms} │ B
3    │ (end G2)        │ G2→Done        │ {unpark@100ms} │
4    │ (idle)          │ (no runnable)  │ {unpark@100ms} │
...
100  │ (timer fires)   │ {}             │ {} → process   │
100  │ pick next       │ G1 Running     │ {}             │
101  │ (resume from    │ G1 Running     │ {}             │
     │  sleep)         │                │                │
```

---

## Key Implementation Rules

### ✅ ALLOWED
- VM calls `scheduler->current()`
- Scheduler modifies goroutine state
- Event loop calls `eventQueue->process()`
- Event callbacks call `scheduler->unpark()`
- Event callbacks call `vm->setGlobal()` (not execute)
- Worker OS thread enqueues callbacks

### ❌ FORBIDDEN
- Worker thread calls `vm->callFunction()`
- Worker thread calls `vm->execute()`
- Timer or channel directly modifies VM state
- VM blocks waiting for anything
- Goroutine code directly manages OS threads
- Nested event loop calls

---

## State Machine

### Goroutine States
```
Created → Runnable → Running → {Suspended | Done}
   ↑                            ↓
   └────────────────────────────┘
              unpark()
```

### VM Execution States
```
Idle → Locked → Running Step → Yield → Idle
             ↑                           ↓
             └───────────────────────────┘
```

---

## Implementation Checklist

- [ ] **EventQueue class**
  - [ ] std::queue<Callback>
  - [ ] processAll() method
  
- [ ] **VM integration**
  - [ ] VM.executeStep() executes ONE instr
  - [ ] VM.yield() suspends current goroutine
  - [ ] VM picks next runnable goroutine
  
- [ ] **Scheduler refactor**
  - [ ] Remove OS threads from Goroutine
  - [ ] Replace task() with bytecode span
  - [ ] Add park/unpark API
  
- [ ] **Channel reimpl**
  - [ ] recv() suspends instead of blocking
  - [ ] send() unparks receiver
  
- [ ] **Timer reimpl**
  - [ ] Never spawn threads
  - [ ] Always enqueue callbacks
  
- [ ] **go redesign**
  - [ ] Spawn coroutine, not OS thread
  - [ ] Return handle with .join(), .send()
  
- [ ] **Main loop redesign**
  - [ ] VM.executeStep()
  - [ ] processScheduler()
  - [ ] eventQueue->processAll()
  - [ ] input polling

---

## File Locations

- **Scheduler:** `src/havel-lang/runtime/concurrency/Scheduler.hpp/.cpp`
- **ConcurrencyBridge:** `src/havel-lang/compiler/runtime/ConcurrencyBridge.hpp/.cpp`
- **VM:** `src/havel-lang/compiler/vm/VM.hpp/.cpp`
- **EventListener:** `src/core/io/EventListener.cpp`
- **EventQueue:** (NEW) `src/havel-lang/compiler/runtime/EventQueue.hpp/.cpp`

---

## Design Locked By
**User Request:** Full concurrency unification before any implementation  
**Violation:** Do not write code until this document is verified complete
