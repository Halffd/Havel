# Phase 3 Gate: Do Not Code Until All Locked

**Status:** BLOCKING GATE  
**Purpose:** Prevent Phase 3 from starting until design is fully locked  
**Date:** April 12, 2026

---

## The Reality

You are not "adding features to the VM."

You are **completely rewriting how the VM executes** to support:
- Per-fiber state (not global)
- Instruction pointer externalization
- Suspension/resumption
- Garbage collection safety
- Exception handling across async boundaries

This is a **fundamental architecture change.** If you get it wrong, you'll spend weeks debugging corruption.

---

## The Gate

**DO NOT PROCEED TO PHASE 3 CODING UNTIL:**

### Design Documents (Must Exist & Be Locked)

- [ ] **PHASE_3_REALITY_CHECK.md** exists
  - Poking holes in what you thought was solid
  - All 7 cracks identified
  - All 5 disaster types explained
  
- [ ] **PHASE_3_DESIGN_SPECIFICATION.md** exists
  - All 11 questions answered with code/structs
  - Main loop written in C++, not prose
  - Fiber struct complete, verified
  - Test cases for all failure modes

### Code Requirements (Must Be Written, Not Just Described)

- [ ] Fiber struct defined in code (not sketch)
  - `struct Fiber` with all fields
  - `FiberStack` separate from VM globals
  - Call frames preserved
  - All accessors working

- [ ] Main loop function written in code
  - `EventListener::executeFrame()` complete
  - EventQueue drain point enforced
  - Scheduler pick integrated
  - executeOneStep called exactly once per frame
  - Result processing (yield/suspend/return/error) all cases

- [ ] executeOneStep signature defined
  - Takes `Fiber*`
  - Returns `VMExecutionResult`
  - Each opcode handler complete (at least 5 major ones)
  - All three exit paths (yield, suspend, return)

### Test Cases (Must Be Defined & Executable)

- [ ] Test: Re-entrance (two fibers yield to each other)
- [ ] Test: Deep stack (nested calls, then yield)
- [ ] Test: Event + suspend ordering (deterministic boundaries)
- [ ] Test: thread.await (no blocking, callbacks work)
- [ ] Test: Yield semantics (resume at ip+1, value returned)
- [ ] Test: GC safety (suspended fiber is root)
- [ ] Test: Exception handling (errors terminate fiber)
- [ ] Test: Stack guard (overflow prevented)

### Review Checklist (Must Be Signed Off)

- [ ] **Peer Review:** Someone else reads the design
  - Sees no obvious flaws
  - Confirms Fiber model is sound
  - Verifies main loop is deterministic
  - Approves test strategy

- [ ] **Architecture Review:** Is this design consistent with Phases 1-2?
  - ✅ EventQueue still the sync boundary
  - ✅ Scheduler still FIFO cooperative
  - ✅ VM thread still single authority
  - ✅ No blocking anywhere
  - ✅ No global state corruption possible

- [ ] **GC Safety Review:** Is suspension GC-safe?
  - ✅ Fiber roots marked in GC
  - ✅ Stack survives suspension
  - ✅ Locals survive suspension
  - ✅ GC only runs at safe points

- [ ] **Exception Safety Review:** Are errors handled?
  - ✅ executeStep catches exceptions
  - ✅ Fiber marked ERROR state
  - ✅ Scheduler picks next (not stuck)
  - ✅ Callbacks fail gracefully

---

## If You Skip This Gate

You will:

1. **Day 1:** Write executeStep(), notice Fiber isn't per-thread safe
2. **Day 2:** Realize call stack corruption across yields
3. **Day 3:** Find out GC isn't tracing suspended fibers
4. **Day 4:** Discover exceptions during suspend crash
5. **Day 5:** Rewrite everything
6. **Week 2+:** Debugging nondeterministic corruption

**Cost:** 2 weeks  
**This gate:** 3 days of design

---

## What Each Section Unlocks

### Once Fiber Struct is Locked
→ You can start implementing VM state access (per-fiber stack, locals)

### Once Main Loop is Locked
→ You know exactly how many times executeStep runs per frame

### Once executeOneStep Signature is Locked
→ You can implement all the opcode handlers

### Once Test Cases are Defined
→ You can write code to pass them

### Once All Reviews Pass
→ You can code Phase 3 with confidence

---

## Sign-Off (Before Proceeding)

**This design is:**

- [ ] Architecturally sound (no broken assumptions)
- [ ] Practically implementable (no impossible requirements)
- [ ] GC-safe (no collector will corrupt suspended fibers)
- [ ] Exception-safe (no lost state on error)
- [ ] Testable (each feature has a test case)
- [ ] Documented (anyone can read and understand)

**I will not code Phase 3 until all boxes are checked.**

---

## Current Status (April 12, 2026)

| Item | Status | Details |
|------|--------|---------|
| PHASE_3_REALITY_CHECK.md | ✅ WRITTEN | All 7 cracks identified, all 5 disasters explained |
| PHASE_3_DESIGN_SPECIFICATION.md | ✅ WRITTEN | All 11 questions answered with code examples |
| Fiber struct | ⏳ DEFINED | Struct definition complete, needs implementation |
| Main loop | ⏳ PARTIALLY | executeFrame signature written, implementation ready |
| executeOneStep | ⏳ PARTIALLY | Signature defined, opcode handlers need impl |
| Test cases | ⏳ WRITTEN | All 8 test cases defined in specification |
| Peer review | ❌ NOT STARTED | Need external eyes on design |
| GC review | ❌ NOT STARTED | Confirm suspended fibers are safe roots |
| Exception review | ❌ NOT STARTED | Confirm error handling is sound |

---

## Next Actions (In Order)

1. **Get feedback on PHASE_3_DESIGN_SPECIFICATION.md**
   - Peer review entire design
   - Ask: "What could break this?"
   - Note: Any major flaws found, fix them

2. **Implement Fiber struct**
   - Create `havel-lang/runtime/Fiber.hpp`
   - Define all fields
   - Write all accessors
   - Compile and verify

3. **Implement Main Loop**
   - Create `EventListener::executeFrame()`
   - Complete all phases (EventQueue, Scheduler, ExecuteStep)
   - Add timing guards

4. **Implement executeOneStep**
   - Load bytecode for current function
   - Switch on opcode
   - Handle all 10+ major opcodes
   - Process exit conditions

5. **Write + Run Tests**
   - Each test case runs a Havel script
   - Verify fiber isolation
   - Verify suspension safety
   - Verify GC roots

---

## The Bitter Truth

If you rush now, you'll code 80% of Phase 3 in 2 weeks, then spend the next 3 weeks debugging memory corruption that's 10 instruction pointers away from where the bug manifests.

If you slow down now, you'll design 100% of Phase 3 in 3 days, then code it in 2 days, and it'll work.

**The choice is yours.**

---

## Sign-Off Line (Final)

I understand the risks.  
I will complete all design before touching Phase 3 code.  
I will have peer review on the specification.  
I will lock the Fiber struct before writing executeStep.  
I will not ship until all 8 tests pass.

**Signed:** ___________________  
**Date:** ___________________
