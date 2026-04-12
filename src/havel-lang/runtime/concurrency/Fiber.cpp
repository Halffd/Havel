#include "Fiber.hpp"

#include <chrono>
#include <stdexcept>

namespace havel::compiler {

// Most Fiber methods are inline in the header for performance
// This .cpp file reserves space for future expansions like:
// - Advanced debugging/profiling hooks
// - Serialization/checkpointing
// - Advanced GC integration

// Explicit instantiation of key methods (optional, for binary size optimization)
template class std::vector<CallFrame>;
template class std::map<std::string, Value>;

}  // namespace havel::compiler
