// C++ test driver for the Cranelift backend prototype (TODO #24).
//
// Links the Rust staticlib (hclb_* C ABI) and verifies:
//   1. the Rust NaN-boxing helpers are bit-identical to C++ havel::core::Value
//      (pack/unpack/tag for ints and bools),
//   2. the C ABI is drivable from C++.
//
// The Rust side runs its own lowering tests (cargo test); this driver pins
// the ABI contract the C++ embedder will rely on when attaching the backend
// through CompilerBackend (src/havel-lang/compiler/core/Backend.hpp).

#include "havel-lang/core/Value.hpp"

#include <cstdint>
#include <cstdio>
#include <limits>

// hclb_* C ABI from the Rust staticlib.
extern "C" {
uint64_t hclb_pack_int48(int64_t v);
int64_t hclb_unpack_int48(uint64_t bits);
bool hclb_is_int48(uint64_t bits);
}

static int failures = 0;

#define CHECK(cond)                                               \
  do {                                                            \
    if (!(cond)) {                                                \
      std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
      ++failures;                                                 \
    }                                                             \
  } while (0)

int main() {
  using havel::core::Value;

  // 1. Int boxing bit-identical to C++ Value::makeInt().rawBits().
  const int64_t ints[] = {
      0, 1, -1, 42, -42,
      static_cast<int64_t>(std::numeric_limits<int32_t>::min()),
      static_cast<int64_t>(std::numeric_limits<int32_t>::max()),
      -(1LL << 40),
  };
  for (int64_t v : ints) {
    const uint64_t cpp_bits = Value::makeInt(v).rawBits();
    const uint64_t rust_bits = hclb_pack_int48(v);
    CHECK(cpp_bits == rust_bits);
    CHECK(hclb_is_int48(cpp_bits));
    CHECK(hclb_unpack_int48(cpp_bits) == v);
  }

  // 2. Non-int words must not read as int48 (bool/null tags are adjacent).
  CHECK(!hclb_is_int48(Value::makeBool(true).rawBits()));
  CHECK(!hclb_is_int48(Value::makeNull().rawBits()));

  // 3. Round-trip through both sides in one expression.
  CHECK(hclb_unpack_int48(hclb_pack_int48(-123456789L)) == -123456789L);

  if (failures == 0) {
    std::printf("OK: cranelift_proto_driver (hclb ABI matches Value bits)\n");
    return 0;
  }
  std::printf("FAILED: %d assertion(s)\n", failures);
  return 1;
}
