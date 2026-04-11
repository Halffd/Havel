#pragma once
// RandomModule.hpp - Random number generation stdlib module
// Uses /dev/urandom for seeding, std::mt19937 for generation

#include <cstdint>

namespace havel {
namespace compiler {
class VMApi;
}

namespace stdlib {

void registerRandomModule(compiler::VMApi &api);

} // namespace stdlib
} // namespace havel
