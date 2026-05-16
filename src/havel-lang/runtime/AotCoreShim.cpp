#include <cstdint>

#if defined(__GNUC__) || defined(__clang__)
#pragma GCC visibility push(default)
#endif
extern "C" void* havel_vm_init_standalone(const char** strings, uint32_t count);

extern "C" void* havel_vm_init_standalone_core(const char** strings, uint32_t count) {
  return havel_vm_init_standalone(strings, count);
}
#if defined(__GNUC__) || defined(__clang__)
#pragma GCC visibility pop
#endif
