// Fixture for tools/gdb/havel.py.
//
// Mirrors the NaN-boxing layout of havel::core::Value plus the shapes the
// havel-* gdb commands decode. Values live in globals so they are always fully
// constructed when the breakpoint hits; with locals, a printer failure and a
// not-yet-initialised variable look identical.
//
// Expected decodings are asserted by test_printers.sh.
#include <cstdint>
#include <cstring>
#include <limits>
#include <optional>
#include <string>
#include <vector>

namespace havel::core {

static constexpr uint64_t QNAN = 0x7FF8000000000000ULL;
static constexpr uint64_t TAG_MASK = 0x0007000000000000ULL;
static constexpr uint64_t PAYLOAD_MASK = 0x0000FFFFFFFFFFFFULL;

enum class ValueTag : uint64_t {
  DOUBLE = 0x0,
  INT48 = 0x1,
  BOOL = 0x2,
  NULL_ = 0x3,
  PTR = 0x4,
  STRING_ID = 0x5,
  OBJECT_ID = 0x6,
  EXTENDED = 0x7,
};

enum class ExtendedTag : uint64_t {
  CLOSURE_ID = 0x0,
  HOST_FUNC_ID = 0x8,
  ENUM_ID = 0x6,
  STRING_VAL_ID = 0xC,
};

struct Value {
private:
  uint64_t bits_;
  static uint64_t makeTaggedRaw(uint64_t tag, uint64_t payload) {
    return QNAN | (tag << 48) | (payload & PAYLOAD_MASK);
  }
  static uint64_t makeExtendedRaw(uint64_t extendedTag, uint64_t payload) {
    return QNAN | (static_cast<uint64_t>(ValueTag::EXTENDED) << 48) |
           (extendedTag << 43) | (payload & 0x000007FFFFFFFFFFULL);
  }
  explicit Value(uint64_t bits) : bits_(bits) {}

public:
  Value() : bits_(makeTaggedRaw(static_cast<uint64_t>(ValueTag::NULL_), 0)) {}
  Value(int i)
      : bits_(makeTaggedRaw(static_cast<uint64_t>(ValueTag::INT48),
                            static_cast<uint64_t>(i) & PAYLOAD_MASK)) {}
  Value(int64_t i)
      : bits_(makeTaggedRaw(static_cast<uint64_t>(ValueTag::INT48),
                            static_cast<uint64_t>(i) & PAYLOAD_MASK)) {}
  Value(bool b)
      : bits_(makeTaggedRaw(static_cast<uint64_t>(ValueTag::BOOL), b ? 1 : 0)) {}
  // Mirrors Value.hpp: an ambiguous NaN double is boxed as null, so a NaN
  // double can never survive construction in the real runtime.
  Value(double d) {
    std::memcpy(&bits_, &d, sizeof(double));
    if ((bits_ & 0x7FF8000000000000ULL) == 0x7FF8000000000000ULL) {
      bits_ = makeTaggedRaw(static_cast<uint64_t>(ValueTag::NULL_), 0);
    }
  }

  static Value makeHostFuncId(uint32_t id) {
    return Value(makeExtendedRaw(
        static_cast<uint64_t>(ExtendedTag::HOST_FUNC_ID), id));
  }
  static Value makeStringId(uint32_t id) {
    return Value(makeTaggedRaw(static_cast<uint64_t>(ValueTag::STRING_ID), id));
  }
  static Value makeObjectId(uint32_t id) {
    return Value(makeTaggedRaw(static_cast<uint64_t>(ValueTag::OBJECT_ID), id));
  }
  static Value makePtr(uint64_t address) {
    return Value(
        makeTaggedRaw(static_cast<uint64_t>(ValueTag::PTR), address & PAYLOAD_MASK));
  }
  // payload 2 is the "default array" bool encoding, not a bool
  static Value makeDefaultArrayBool() {
    return Value(makeTaggedRaw(static_cast<uint64_t>(ValueTag::BOOL), 2));
  }
  static Value makeRawBoolPayload(uint64_t payload) {
    return Value(makeTaggedRaw(static_cast<uint64_t>(ValueTag::BOOL), payload));
  }
  // any sub-tag not present in the printer's table, to exercise the fallback
  static Value makeUnknownExtended(uint64_t extendedTag, uint64_t payload) {
    return Value(makeExtendedRaw(extendedTag, payload));
  }
  static Value makeStringValId(uint32_t id, uint32_t chunkId = 0) {
    uint64_t payload = (static_cast<uint64_t>(chunkId & 0xFFF) << 31) |
                       (id & 0x7FFFFFFF);
    return Value(
        makeExtendedRaw(static_cast<uint64_t>(ExtendedTag::STRING_VAL_ID), payload));
  }
  static Value makeEnumId(uint32_t id, uint32_t typeId = 0) {
    uint64_t payload = (static_cast<uint64_t>(typeId & 0x7FF) << 32) | id;
    return Value(makeExtendedRaw(static_cast<uint64_t>(ExtendedTag::ENUM_ID), payload));
  }
};

} // namespace havel::core

namespace havel::compiler {

using Value = havel::core::Value;

struct SourceLocation {
  std::string filename;
  uint32_t line = 0;
  uint32_t column = 0;
  uint32_t length = 0;
};

enum class OpCode { NOP = 0, LOAD_CONST = 3 };

struct Instruction {
  OpCode opcode;
  std::vector<Value> operands;
  std::optional<SourceLocation> location;
};

struct HostFunctionInfo {
  std::string name;
  uint32_t index = 0;
  std::optional<size_t> arity;
  std::string module;
  std::string namespace_prefix;
  bool callable = false;
  bool bound_as_global = false;
};

struct VM {
  std::vector<std::string> host_function_names_;
  // Out of line, like the real VM::resolveStringKey in VMValue.cpp. GDB
  // cannot call an inline body from an expression ("may be inlined").
  std::string resolveStringKey(const Value &value) const;
};

} // namespace havel::compiler

std::string havel::compiler::VM::resolveStringKey(const Value &) const {
  return "resolved-string";
}

using namespace havel::compiler;

// Seeded during static initialization so the registry is populated at every
// breakpoint, including the first line of main. Assigning in main instead would
// make host-function name resolution depend on where the breakpoint lands.
static VM vm = [] {
  VM seeded;
  seeded.host_function_names_ = {"math.sqrt", "bit._popcount"};
  return seeded;
}();
// A VM reached through a pointer, and through a const pointer, is how a VM
// actually appears inside VM methods (`this`).
static VM *vm_ptr = &vm;
static const VM *vm_const_ptr = &vm;
static Value g_double = 3.5;
static Value g_double_neg_inf = -std::numeric_limits<double>::infinity();
static Value g_negative = -12345;
static Value g_int_min = static_cast<Value>(static_cast<int64_t>(-140737488355328LL));
static Value g_int_max = static_cast<Value>(static_cast<int64_t>(140737488355327LL));
static Value g_true = true;
static Value g_false = false;
static Value g_nil;
static Value g_host_fn = Value::makeHostFuncId(1);
static Value g_string_val = Value::makeStringValId(7, 3);
static Value g_enum = Value::makeEnumId(5, 66);
// index needs > 16 bits to catch a truncated low mask; typeId > 1023 is
// impossible (11 bits), so only the index side is pushed wide.
static Value g_enum_wide_index = Value::makeEnumId(70000, 3);
// typeId needs more than 8 bits to catch a truncated mask on the type field.
static Value g_enum_wide = Value::makeEnumId(3, 511);
static Value g_string_id = Value::makeStringId(48);
static Value g_object_id = Value::makeObjectId(9);
static Value g_ptr = Value::makePtr(0xDEADBEEF);
static Value g_default_array = Value::makeDefaultArrayBool();
static Value g_odd_bool = Value::makeRawBoolPayload(7);
static Value g_array = Value::makeUnknownExtended(0x01, 12);
static Value g_unknown_ext = Value::makeUnknownExtended(0x1F, 34);
static Value g_regex = Value::makeUnknownExtended(0x11, (2ULL << 31) | 4);
// chunk id needs more than 8 bits to catch a truncated mask on the chunk field
static Value g_string_val_wide = Value::makeStringValId(9, 4095);
// string-val index field is 31 bits. All bits set (2147483647) so a
// narrower low mask truncates it and the decode differs.
static Value g_string_val_wide_index = Value::makeStringValId(0x7FFFFFFF, 4095);
static Value g_double_inf = std::numeric_limits<double>::infinity();
static Value g_double_nan = std::numeric_limits<double>::quiet_NaN();
static std::vector<Value> g_values = {g_double, g_negative, g_true, g_nil,
                                      g_host_fn, g_string_val, g_enum};
static std::vector<Value> g_empty_values;
static Instruction g_instruction{OpCode::LOAD_CONST,
                                 {g_negative, g_nil},
                                 SourceLocation{"probe.hv", 12, 4, 9}};
static HostFunctionInfo g_host_fn_info{"bit._popcount", 1, 2, "bit", "bit", true,
                                       true};
static HostFunctionInfo g_unimplemented{"process.spawn", 2, std::nullopt, "", "",
                                        false, false};
static std::vector<HostFunctionInfo> g_host_fn_infos = {g_host_fn_info,
                                                        g_unimplemented};

int main() {
  // Referenced so the pointer globals are emitted into debug info and are
  // resolvable by name from a breakpoint.
  return (vm_ptr == &vm && vm_const_ptr == &vm) ? 0 : 1;
}
