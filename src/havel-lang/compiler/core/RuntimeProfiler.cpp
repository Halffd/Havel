#include "RuntimeProfiler.hpp"

#include <sstream>

namespace havel::compiler {

std::string RuntimeProfiler::summary() const {
  std::ostringstream out;
  out << "profiler: calls=" << totalCalls()
      << " backedges=" << totalBackedges()
      << " instructions=" << instructions()
      << " throws=" << throws()
      << " allocations=" << allocations()
      << " tier1=" << tier1Compiles()
      << " tier2=" << tier2Compiles();

  // Top hot functions by invocation count (bounded scan; the array is
  // fixed-size so this is cheap and only runs on demand).
  uint32_t top_index = 0;
  uint64_t top_calls = 0;
  for (uint32_t i = 0; i < kMaxTrackedFunctions; ++i) {
    const uint64_t calls = function_calls_[i].load(std::memory_order_relaxed);
    if (calls > top_calls) {
      top_calls = calls;
      top_index = i;
    }
  }
  if (top_calls > 0) {
    out << " hottest_fn_index=" << top_index
        << " hottest_fn_calls=" << top_calls;
  }
  return out.str();
}

}  // namespace havel::compiler
