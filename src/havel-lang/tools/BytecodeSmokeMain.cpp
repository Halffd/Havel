#include "havel-lang/compiler/bytecode/SourceBytecodePipeline.hpp"

#include <cstdint>
#include <exception>
#include <iostream>
#include <string>

namespace {

bool equalsInt(const havel::compiler::BytecodeValue &value, int64_t expected) {
  if (!std::holds_alternative<int64_t>(value)) {
    return false;
  }
  return std::get<int64_t>(value) == expected;
}

int runCase(const std::string &name, const std::string &source,
            int64_t expected) {
  try {
    const auto result = havel::compiler::runBytecodePipeline(source);
    if (!equalsInt(result.return_value, expected)) {
      std::cerr << "[FAIL] " << name << ": expected " << expected
                << " but got non-matching result" << std::endl;
      return 1;
    }

    std::cout << "[PASS] " << name << std::endl;
    return 0;
  } catch (const std::exception &e) {
    std::cerr << "[FAIL] " << name << ": exception: " << e.what()
              << std::endl;
    return 1;
  }
}

int runClosureBoundaryCase() {
  const std::string source = R"havel(
fn outer() {
    let x = 1
    fn inner() {
        return x
    }
    return inner()
}

return outer()
)havel";

  try {
    (void)havel::compiler::runBytecodePipeline(source);
    std::cerr << "[FAIL] closure-boundary: expected Phase 2 boundary error"
              << std::endl;
    return 1;
  } catch (const std::exception &e) {
    const std::string message = e.what();
    if (message.find("Phase 2") == std::string::npos) {
      std::cerr << "[FAIL] closure-boundary: wrong error: " << message
                << std::endl;
      return 1;
    }

    std::cout << "[PASS] closure-boundary" << std::endl;
    return 0;
  }
}

} // namespace

int main() {
  int failures = 0;

  failures += runCase("function-call", R"havel(
fn add(a, b) {
    return a + b
}

let x = add(2, 3)
print(x)
return x
)havel", 5);

  failures += runCase("while-loop", R"havel(
let i = 0
let sum = 0

while i < 4 {
    let sum = sum + i
    let i = i + 1
}

return sum
)havel", 6);

  failures += runClosureBoundaryCase();

  if (failures != 0) {
    std::cerr << "Bytecode smoke failed with " << failures << " failing case(s)"
              << std::endl;
    return 1;
  }

  std::cout << "Bytecode smoke passed" << std::endl;
  return 0;
}
