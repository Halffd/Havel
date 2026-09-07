#pragma once

// ===== Runtime profiler (TODO.md #26) =====
//
// Lightweight runtime profiling with lock-free hot paths. Tracks the
// counters the tiering system and future PGO work consume:
//
//   - function invocation counts
//   - per-instruction feedback execution counts (owned by TypeFeedback,
//     reported here in aggregate)
//   - loop backedge iteration counts (per function)
//   - thrown exceptions (caught and uncaught)
//   - heap allocations
//   - total interpreter instructions and approximate execution time
//
// Every counter is a relaxed atomic increment: no locks, no allocation,
// no per-call formatting on the hot path. Report formatting happens only
// when a consumer asks (hvdb status, shutdown summary, tiering hooks).
//
// The profiler deliberately does NOT try to map per-IP feedback through
// the optimizer's instruction rewrites: the OptimizerDriver clears stale
// per-IP feedback and re-derives AOT hints (see OptimizerDriver.hpp).
// What survives optimization is per-function hotness, which is what
// tier selection needs; per-site PGO mapping is future work gated on
// proven counters (TODO #26: do not add complex PGO before the basic
// counters are trustworthy).

#include <atomic>
#include <cstdint>
#include <string>
#include <unordered_map>

namespace havel::compiler {

class RuntimeProfiler {
public:
  // --- Hot-path recording (lock-free, relaxed ordering) ---

  void recordFunctionCall(uint32_t function_index) noexcept {
    if (function_index < kMaxTrackedFunctions) {
      function_calls_[function_index].fetch_add(1, std::memory_order_relaxed);
    }
    total_calls_.fetch_add(1, std::memory_order_relaxed);
  }

  void recordBackedge(uint32_t function_index) noexcept {
    if (function_index < kMaxTrackedFunctions) {
      backedges_[function_index].fetch_add(1, std::memory_order_relaxed);
    }
    total_backedges_.fetch_add(1, std::memory_order_relaxed);
  }

  // Total-only backedge recording for sites without a resolved function
  // index (e.g. before the frame lookup); the total still feeds hotness.
  void recordBackedgeTotal() noexcept {
    total_backedges_.fetch_add(1, std::memory_order_relaxed);
  }

  void recordThrow() noexcept {
    throws_.fetch_add(1, std::memory_order_relaxed);
  }

  void recordAllocation() noexcept {
    allocations_.fetch_add(1, std::memory_order_relaxed);
  }

  // Direct sink for external allocation sites (the GC heap): avoids a virtual
  // or callback indirection per allocation; the sink is the profiler's own
  // atomic so the GC only performs one relaxed fetch_add.
  std::atomic<uint64_t>* allocationSink() noexcept { return &allocations_; }

  void recordInstruction() noexcept {
    instructions_.fetch_add(1, std::memory_order_relaxed);
  }

  // Batched instruction recording for the dispatch loop's periodic
  // checkpoint (every 8192 instructions): one relaxed add per batch.
  void recordInstructions(uint64_t batch) noexcept {
    instructions_.fetch_add(batch, std::memory_order_relaxed);
  }

  void recordTier1Compile(const std::string& function_name) {
    tier1_compiles_.fetch_add(1, std::memory_order_relaxed);
    (void)function_name;
  }

  void recordTier2Compile(const std::string& function_name) {
    tier2_compiles_.fetch_add(1, std::memory_order_relaxed);
    (void)function_name;
  }

  // --- Queries ---

  uint64_t functionCalls(uint32_t function_index) const {
    if (function_index < kMaxTrackedFunctions) {
      return function_calls_[function_index].load(std::memory_order_relaxed);
    }
    return 0;
  }

  uint64_t backedges(uint32_t function_index) const {
    if (function_index < kMaxTrackedFunctions) {
      return backedges_[function_index].load(std::memory_order_relaxed);
    }
    return 0;
  }

  uint64_t totalCalls() const {
    return total_calls_.load(std::memory_order_relaxed);
  }
  uint64_t totalBackedges() const {
    return total_backedges_.load(std::memory_order_relaxed);
  }
  uint64_t throws() const { return throws_.load(std::memory_order_relaxed); }
  uint64_t allocations() const {
    return allocations_.load(std::memory_order_relaxed);
  }
  uint64_t instructions() const {
    return instructions_.load(std::memory_order_relaxed);
  }
  uint64_t tier1Compiles() const {
    return tier1_compiles_.load(std::memory_order_relaxed);
  }
  uint64_t tier2Compiles() const {
    return tier2_compiles_.load(std::memory_order_relaxed);
  }

  // Formatted summary for diagnostics (hvdb status, shutdown report).
  std::string summary() const;

  // Reset all counters (used between runs in persistent/REPL sessions).
  void reset() {
    for (auto& c : function_calls_) c.store(0, std::memory_order_relaxed);
    for (auto& c : backedges_) c.store(0, std::memory_order_relaxed);
    total_calls_.store(0, std::memory_order_relaxed);
    total_backedges_.store(0, std::memory_order_relaxed);
    throws_.store(0, std::memory_order_relaxed);
    allocations_.store(0, std::memory_order_relaxed);
    instructions_.store(0, std::memory_order_relaxed);
    tier1_compiles_.store(0, std::memory_order_relaxed);
    tier2_compiles_.store(0, std::memory_order_relaxed);
  }

  // Per-function index space: functions are indexed by their chunk-local
  // function index; the array is bounded and silently saturates beyond it.
  static constexpr uint32_t kMaxTrackedFunctions = 4096;

private:
  std::atomic<uint64_t> function_calls_[kMaxTrackedFunctions] = {};
  std::atomic<uint64_t> backedges_[kMaxTrackedFunctions] = {};
  std::atomic<uint64_t> total_calls_{0};
  std::atomic<uint64_t> total_backedges_{0};
  std::atomic<uint64_t> throws_{0};
  std::atomic<uint64_t> allocations_{0};
  std::atomic<uint64_t> instructions_{0};
  std::atomic<uint64_t> tier1_compiles_{0};
  std::atomic<uint64_t> tier2_compiles_{0};
};

}  // namespace havel::compiler
