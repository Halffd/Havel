# Phase 3 Reality Check: Gaps That Will Break You

**Date:** April 12, 2026  
**Status:** CRITICAL DESIGN ISSUES IDENTIFIED BEFORE CODING  
**Risk Level:** CATASTROPHIC if ignored

---

## 1. THE EXECUTION LOOP DOESN'T EXIST

### What We Have
```
"VM + Scheduler cooperate"
"EventQueue processes callbacks"
```

### What We Don't Have
**An actual, written, strict main loop that enforces the model.**

### The Trap
If the loop looks like:
```cpp
while (running) {
    vm.execute();              // ❌ WRONG: runs arbitrary steps
    scheduler.pickNext();      // ❌ WRONG: might be dead code
    eventQueue.processAll();   // ❌ WRONG: order undefined
}
```

Then:
- VM might run 1000 steps before yielding (scheduler never picks)
- EventQueue might be drained while VM is mid-computation  
- Goroutines might run out of order
- **The whole determinism guarantee collapses**

### The Real Rule (MUST BE ENFORCED)
```cpp
while (running) {
    eventQueue.processAll();           // (1) External events
    Goroutine* fiber = scheduler.pickNext();  // (2) Next fiber or main
    if (fiber) {
        current_fiber_ = fiber;
        execution_state = executeOneStep(fiber);  // (3) EXACTLY ONE
        
        if (execution_state == SUSPENDED) {
            scheduler.suspend(fiber, reason);
        } else if (execution_state == RETURNED) {
            scheduler.markDone(fiber);
            if (scheduler.hasYielded()) {
                eventQueue.push(resumeFiberCallback);  // Wake on yield
            }
        }
    }
}
```

**Without this exact pattern:** You have nothing.

---

## 2. SCHEDULER MISSING: PER-FIBER VM STATE

### What We Defined
```cpp
struct Goroutine {
    uint32_t function_id;
    uint32_t ip;
    // ... list metadata ...
};
```

### What We're Missing
```cpp
struct Fiber {
    // BYTECODE POSITION (required)
    uint32_t function_id;
    uint32_t ip;
    
    // ❌ MISSING: VM STATE
    Stack* stack;           // MUST be per-fiber, not global VM.stack
    std::vector<CallFrame> frames;  // MUST be per-fiber
    std::map<string, Value> locals; // MUST be per-fiber
    
    // EXECUTION CONTEXT (required for resumption)
    VMState execution_state;
    SuspensionReason suspension_reason;
    
    // SYNC PRIMITIVES (required)
    void* waiting_on;  // Channel? Thread? Timer?
};
```

### Why This Breaks Everything

**Current design assumes:** Goroutines share the global VM state (stack, frames, locals)

**Reality:** If two goroutines run:
```
G1: push a, push b
G2: pop, pop
G1: resume (expects a, b still on stack) → ❌ CRASH
```

**Solution:** Every goroutine gets its own **Fiber** that holds:
- Its own stack
- Its own call frames
- Its own local variable map

### The Requirement

**Before Phase 3:** Define `struct Fiber` and verify that:
1. Goroutine creation initializes a new Fiber
2. Scheduler.pickNext() returns a Fiber, not a Goroutine
3. VM.executeStep() operates on current Fiber's stack/frames, NOT global state
4. Suspension saves Fiber.ip, Fiber.state
5. Resumption restores from Fiber, not from global VM state

---

## 3. EVENTQUEUE ORDERING ≠ EXECUTION DETERMINISM

### What We Claim
```
✅ All callbacks processed in order (FIFO)
```

### What That Doesn't Guarantee
```
❌ Callbacks execute in deterministic order relative to VM steps
```

### Example That Breaks
```hv
interval 100 {
    print "A"           // callback enqueued
}

yield 1
yield 2
yield 3
```

**What happens:**
```
t=0: interval timer fires → push callback-A to eventQueue
t=0: VM step 1: yield 1
t=0: VM step 2: yield 2  
t=0: VM step 3: yield 3
t=0: eventQueue.processAll()  ← callback-A runs NOW
     print "A"

Output: 1, 2, 3, A

But you might expect: A, 1, 2, 3 (since timer fired first)
```

### The Fix: Deterministic Drain Points

**Rule:** EventQueue must be drained at KNOWN BOUNDARIES:

```cpp
while (running) {
    eventQueue.processAll();        // DRAIN POINT 1
    
    Goroutine* g = scheduler.pickNext();
    if (g) {
        executeOne(g);
    }
    
    // DO NOT drain queue in middle of goroutine execution
    // DO NOT drain queue during VM step
}
```

### What This Means

External events (timers, I/O, channels) can enqueue callbacks

But those callbacks **ONLY RUN** at explicit synchronization points:
- Before each VM step
- After goroutine suspension
- Never during execution

**Buried in your code:** If any function calls `eventQueue.processAll()` from within a callback, you've lost determinism.

---

## 4. SYNCHRONIZATION BOUNDARY NOT ENFORCED

### Current Design
```
EventQueue is the only way to the VM
(good in theory)
```

### Ways This Can Break
```
1. Timer thread directly calls VM methods
2. Channel recv() directly modifies scheduler
3. I/O callback bypasses EventQueue
4. GC triggers from worker thread
```

### The Enforcement Rule

**Every. Single. Callback. Must. Be. In. EventQueue.**

This means:
- ❌ No direct function calls from OS threads to VM
- ❌ No condition_variable wakeups to VM code
- ❌ No shared memory writes except through EventQueue callbacks
- ✅ OS threads ONLY call `eventQueue.push(callback)`

### How to Verify

**Before Phase 3:** Search codebase for:
```cpp
// BANNED:
scheduler->unpark(g);  // ← Called from worker thread?
value_ptr->set();      // ← Modified from timer?
vm->execute();         // ← Called from I/O handler?

// REQUIRED:
eventQueue->push([scheduler, g]() { 
    scheduler->unpark(g);  // ✅ Via callback
});
```

If ANY OS thread talks to scheduler/VM directly → undefined behavior.

---

## 5. thread.join() — INCOMPLETE DESIGN

### What We Have
```
"Identified blocking call"
"Solution: park goroutine + callback"
```

### What We Don't Have
```
- Actual code pattern
- Integration with Fiber state
- Test case
```

### The Real Pattern

**In Havel code:**
```hv
t = thread fn { print "hi" }
result = await t  // ← How does this work??
```

**What must happen:**
```
1. Goroutine calls thread.await(t)
2. Check if thread done:
   - YES: return result (no yield needed)
   - NO: suspend goroutine with reason=ThreadWait, waiting_on=t
3. Thread completion handler:
   - Enqueue callback to EventQueue
   - Callback does: scheduler.unpark(waiting_fiber)
4. Main loop:
   - EventQueue drains
   - Scheduler picks waiting_fiber
   - Fiber resumes after await
```

### The Dangerous Trap

**If you ever let a goroutine call `thread.join()`:**
```
blocking call
→ blocks goroutine
→ blocks VM thread
→ VM thread blocks
→ no event processing
→ thread never signals completion
→ deadlock
```

**Solution:** `await` not `join`. `suspend` not `block`.

### Test Case (Must Pass)
```hv
t1 = thread { 
    sleep 100  // 100ms
}
print "before"
r1 = await t1
print "after"
yield something_else  // Other work happens during 100ms
```

Verify:
- "before" prints before await
- Other work runs while thread sleeps (no blocking)
- "after" prints after thread completes

---

## 6. YIELD SEMANTICS UNDEFINED

### Current State
```cpp
bytecode.YIELD → runtime.yield(value)
→ returns null to caller
```

### The Duck-Typing Disaster

Right now we're mixing:
- Function return
- Coroutine suspension
- Generator/iterator pattern

All in the same bytecode instruction.

### What Must Be Defined

**Question:** Does `yield` suspend or return?

**Answer:** BOTH, in control flow, but ONE in object model.

```hv
fn task {
    print "A"
    x = yield 1      // Suspend, return 1 to caller
    print "B"
    yield 2          // Suspend, return 2 to caller  
    print "C"
    return 42        // Return from generator, close
}

next = coroutine task  // Create fiber

print next()    // "A", 1
print next()    // "B", 2
print next()    // "C", 42
```

### Hard Rules Required

**Rule 1:** `yield value` suspends execution, returns value to caller
**Rule 2:** Next `next()` resumes after yield
**Rule 3:** `return value` terminates generator, closes handle
**Rule 4:** Calling next() after return throws
**Rule 5:** Suspension state includes: `(function_id, ip, reason=YieldedByUser)`

### Bytecode Model

```
YIELD val    → suspend(fiber, reason=Yield), push(val), return to caller
RETURN val   → mark done, cleanup, return val
```

### The Verification Test
```hv
fn yield_three {
    yield "A"
    yield "B"  
    yield "C"
    return 42
}

g = coroutine yield_three
assert g() == "A"
assert g() == "B"
assert g() == "C"
assert g() == 42
assert (try g() catch) == "generator closed"  // No more yields
```

**If this test fails:** Your yield semantics are broken.

---

## 7. THE REAL PHASE 3 CHALLENGE

### What You Think Phase 3 Is
```
"Convert VM.execute() to VM.executeStep()"
```

### What It Actually Is
```
"Convert a batch processor into a resumable bytecode interpreter"
```

### The Difficulty Breakdown

**Easy (20%):**
- Rename execute() to executeStep()
- Return after 1 instruction

**Hard (50%):**
- Externalize instruction pointer
- Preserve call stack across suspension
- Manage temporary values through yield

**Nightmare (30%):**
- GC correctness while suspending
- Exception handling across suspension
- Nested function calls with local variables
- Tail recursion optimization

### Minimum Requirements

Before coding Phase 3, you MUST have:

1. ✅ **Fiber struct** with complete VM state
2. ✅ **Per-fiber stack** (not shared global)
3. ✅ **Per-fiber call frames** (not shared global)
4. ✅ **Per-fiber locals** (not shared global)
5. ✅ **Instruction pointer persistence** (saved/restored)
6. ✅ **Actual main loop** written in code (not docs)
7. ✅ **Yield semantics** formally defined
8. ✅ **thread.await pattern** designed
9. ✅ **EventQueue enforcement rules** documented
10. ✅ **Test cases** that would catch each failure mode

### The Unspoken Reality

If you code Phase 3 without these 10 things locked down:

You'll write 2000 lines of code
Then find out you got the Fiber model wrong
Then rewrite everything

But doing it right takes 3 days of design, 1 day of coding.

---

## 8. TESTS THAT MUST PASS (BEFORE PHASE 3 CODE)

### Test 1: Re-entrance
```hv
go task1() {
    yield 1
    yield 2
}

go task2() {
    yield "a"
    yield "b"
}

// Both running, both suspending
// Must not corrupt each other's stack
```

**Verify:** Each fiber has independent stack. No cross-contamination.

### Test 2: Deep Stack
```hv
fn a() { b() }
fn b() { c() }
fn c() { d() }
fn d() { yield 42 }

g = coroutine a
print g()  // Must print 42, not crash
```

**Verify:** Call frame chain preserved through suspension. Locals intact on resume.

### Test 3: Event + Suspend Ordering
```hv
interval 10 {
    print "tick"
}

go task() {
    print "A"
    yield 1
    print "B"
    yield 2
}
```

**Verify:** Events run at deterministic boundaries. Ticks don't fire mid-yield.

### Test 4: thread.await
```hv
t = thread {
    sleep 50
    return 99
}
print "before"
result = await t
print "after"
print result

// Must print: before, after, 99
// Never block the main thread
```

**Verify:** await suspends fiber, not VM. Main loop responsive.

### Test 5: Yield While Suspended
```hv
chan = channel()

task1() {
    yield 1
    x = recv chan
    yield 2
}

task2() {
    yield "a"
    send chan 99
    yield "b"
}

// Both yield, both recv/send, both yield again
```

**Verify:** Suspension reason matters. Channel ops integrate with yield.

---

## 9. WHAT KILLS MOST ASYNC RUNTIMES

### Mistake 1: Global VM State
```cpp
class VM {
    Stack stack;           // ❌ Global
    CallFrame frames[];    // ❌ Global
};

// Two goroutines both touch VM.stack
// Now it's corrupted
```

### Mistake 2: Blocking I/O
```cpp
eventQueue->push([io]() {
    io.read();  // ❌ BLOCKS EVENT LOOP
});
```

### Mistake 3: Lost Instruction Pointer
```cpp
void fiber::yield() {
    // ❌ Where do we store IP to resume?
    // ❌ Bytecode pointer is lost
    // ❌ Resume crashes
}
```

### Mistake 4: Forgotten GC Roots
```cpp
// Suspend while in middle of GC
// Fiber's temporaries are on VM stack
// GC doesn't see them as roots
// ❌ Corruption
```

### Mistake 5: Exception Handling
```cpp
try {
    yield x
} catch {
    // ❌ Exception during what? Yield? Resume?
    // ❌ What state are we in?
}
```

---

## 10. YOUR POSITION RIGHT NOW

### What You Got Right
✅ Architecture: VM thread → Scheduler → EventQueue  
✅ Direction: Single execution authority  
✅ No OS threads in scheduler  
✅ Callbacks as sync primitive  

### What You Haven't Defined
❌ Per-fiber VM state (Fiber struct)  
❌ Actual main loop (written in code)  
❌ Yield vs return semantics  
❌ thread.await pattern  
❌ EventQueue enforcement rules  
❌ GC correctness during suspension  
❌ Exception semantics  

### What Will Happen If You Code Phase 3 Now
```
Day 1: Write executeStep()
Day 2: Add Fiber struct halfway through, rewrite executeStep()
Day 3: Discover global state corruption, rewrite again
Day 4: Yield semantics undefined, crash on second yield
Day 5: Realize GC unsafe, rewrite again
Week 2: All working but fragile

vs.

3 days: Define exactly what you're building
1 day: Code it right once
```

---

## 11. THE PATH FORWARD

### STOP: Don't code Phase 3 yet

### DO: Answer These Questions

1. **Fiber struct:** Define complete per-goroutine VM state
2. **Main loop:** Write actual code that enforces the pattern
3. **Yield:** Formal definition (suspend + value return)
4. **thread.await:** Exact state transitions
5. **GC:** How does it stay safe across suspension?
6. **Exceptions:** What happens if exception during yield?
7. **Stack limits:** What if fiber does deep recursion?
8. **Memory:** How much per-fiber state? Stacklets or heap?

### Document: PHASE_3_DESIGN_SPECIFICATION.md

Before writing ONE LINE of Phase 3 code, you need:
- Fiber struct definition (can be pseudocode)
- Main loop pseudocode
- State transition diagram
- Test cases that define behavior
- GC safety proof (or plan)

### Then: Code Phase 3 From Spec

If spec is right, Phase 3 is straightforward.

If spec is incomplete, you'll figure it out in 3 days instead of 3 weeks.

---

## Conclusion

You have the architecture right.

But architecture ≠ implementation.

The gap between "EventQueue processes events" and "here's exactly how EventQueue, Scheduler, and VM coordinate" is where bugs live.

**Don't let reality poke holes. You poke them first.**

---

**NEXT ACTION:** 
Not Phase 3 code.
Not more docs.

**ANSWER THE 11 DESIGN QUESTIONS.**

Then you can code fearlessly.
