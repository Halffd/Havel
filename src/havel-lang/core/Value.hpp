/*
 * Value.hpp
 *
 * NaN-boxed value representation for Havel language runtime.
 *
 * Layout (canonical NaN boxing):
 *   sign (1) | exponent (11 = 0x7FF) | mantissa (52)
 *   mantissa layout:
 *     [quiet NaN bit = 1] [tag 3 bits] [payload 48 bits]
 *
 * Tags:
 *   0b000 = double (not NaN - actual floating point value)
 *   0b001 = int48 (48-bit signed integer)
 *   0b010 = bool
 *   0b011 = null
 *   0b100 = pointer (raw pointer to heap object)
 *   0b101 = string id (32-bit index into GC string table)
 *   0b110 = object id (32-bit index into GC object table)
 *   0b111 = extended tag (uses extra bits for more type discrimination)
 *
 * Doubles pass through completely untouched - zero cost for the most common case.
 * Non-double types are boxed into the NaN space.
 */
#pragma once

#include <cstdint>
#include <cstring>
#include <functional>
#include <memory>
#include <optional>
#include <sstream>
#include <string>
#include <unordered_map>
#include <variant>
#include <vector>

namespace havel::core {

// ============================================================================
// NaN Boxing Constants
// ============================================================================

static constexpr uint64_t QNAN = 0x7FF8000000000000ULL;
static constexpr uint64_t TAG_MASK = 0x0007000000000000ULL;
static constexpr uint64_t PAYLOAD_MASK = 0x0000FFFFFFFFFFFFULL;
static constexpr uint64_t INT48_SIGN_BIT = 0x0000800000000000ULL;

// Primary tag values (bits 48-50 in mantissa)
enum class ValueTag : uint64_t {
  DOUBLE = 0x0,    // Not actually tagged - real doubles are just doubles
  INT48 = 0x1,
  BOOL = 0x2,
  NULL_ = 0x3,
  PTR = 0x4,
  STRING_ID = 0x5,
  OBJECT_ID = 0x6,
  EXTENDED = 0x7,  // Extended tags use bits 45-47 for sub-type
};

// Extended tag values (bits 45-47 when primary tag is EXTENDED)
enum class ExtendedTag : uint64_t {
  CLOSURE_ID = 0x0,
  ARRAY_ID = 0x1,
  SET_ID = 0x2,
  RANGE_ID = 0x3,
  ENUM_ID = 0x6,
  ITERATOR_ID = 0x7,
  HOST_FUNC_ID = 0x8,  // Host function reference (stores index into table)
  LAZY_PIPELINE_ID = 0x9,
  ERROR_ID = 0xA,
  FUNCTION_OBJ_ID = 0xB,  // FunctionObject (stores function_index)
  STRING_VAL_ID = 0xC,    // Inline string (stores index into string pool)
  THREAD_ID = 0xD,        // Thread object (stores index into GC heap)
  INTERVAL_ID = 0xE,      // Interval timer (stores index into GC heap)
  TIMEOUT_ID = 0xF,       // Timeout timer (stores index into GC heap)
};

// Bool payload values
static constexpr uint64_t BOOL_FALSE = 0;
static constexpr uint64_t BOOL_TRUE = 1;

// Extended tag mask (bits 44-47, 4 bits for values 0-15)
// Positioned below the primary tag (bits 48-50) to avoid overlap
static constexpr uint64_t EXTENDED_TAG_MASK = 0x0000F00000000000ULL;
static constexpr int EXTENDED_TAG_SHIFT = 44;

// ============================================================================
// Forward declarations for GC object types
// ============================================================================

// Forward declare Value
struct Value;

// Basic value container types (for pointer-based objects)
using ArrayValue = std::shared_ptr<std::vector<Value>>;
using ObjectValue =
    std::shared_ptr<std::unordered_map<std::string, Value>>;

// Built-in function type
using BuiltinFunction = std::function<Value(const std::vector<Value> &)>;

/**
 * Runtime error wrapper
 */
struct RuntimeError {
  std::string message;
  size_t line = 0;
  size_t column = 0;
  bool hasLocation = false;

  RuntimeError() = default;
  explicit RuntimeError(const std::string &msg) : message(msg) {}
  RuntimeError(const std::string &msg, size_t l, size_t c)
      : message(msg), line(l), column(c), hasLocation(true) {}
};

/**
 * Control flow wrappers - must be defined BEFORE Result
 * Use shared_ptr to avoid incomplete type issues
 */
struct ReturnValue {
  std::shared_ptr<Value> value;
};

struct BreakValue {};
struct ContinueValue {};

/**
 * Result type - wraps all possible evaluation outcomes
 */
using Result =
    std::variant<Value, RuntimeError, ReturnValue, BreakValue, ContinueValue>;

/**
 * NaN-boxed Value
 *
 * 64-bit representation using quiet NaN space for type tagging.
 * - Real doubles pass through untouched (zero cost)
 * - Other types are encoded in the NaN mantissa space
 */
struct Value {
private:
  uint64_t bits_;

  // Internal constructor for raw bits
  explicit Value(uint64_t bits) : bits_(bits) {}

  // Helper: check if bits represent a boxed (NaN-tagged) value
  static bool isBoxed(uint64_t bits) {
    return (bits & 0x7FF8000000000000ULL) == 0x7FF8000000000000ULL;
  }

  // Helper: extract primary tag from boxed value
  static ValueTag extractTag(uint64_t bits) {
    return static_cast<ValueTag>((bits & TAG_MASK) >> 48);
  }

  // Helper: extract extended tag from boxed value
  static ExtendedTag extractExtendedTag(uint64_t bits) {
    return static_cast<ExtendedTag>((bits & EXTENDED_TAG_MASK) >> EXTENDED_TAG_SHIFT);
  }

  // Helper: extract 48-bit payload
  static uint64_t extractPayload(uint64_t bits) { return bits & PAYLOAD_MASK; }

  // Helper: sign-extend 48-bit integer to 64-bit
  static int64_t signExtend48(uint64_t payload) {
    // If sign bit (bit 47) is set, extend with 1s
    if (payload & INT48_SIGN_BIT) {
      return static_cast<int64_t>(payload | 0xFFFF000000000000ULL);
    }
    return static_cast<int64_t>(payload);
  }

  // Helper: truncate 64-bit integer to 48-bit (mask)
  static uint64_t truncate48(int64_t value) {
    return static_cast<uint64_t>(value) & PAYLOAD_MASK;
  }

public:
  // ========================================================================
  // Constructors
  // ========================================================================

  Value() : bits_(makeNullRaw()) {}
  Value(std::nullptr_t) : bits_(makeNullRaw()) {}

  Value(bool b) : bits_(makeBoolRaw(b)) {}

  Value(int i) : bits_(makeIntRaw(static_cast<int64_t>(i))) {}
  Value(int64_t i) : bits_(makeIntRaw(i)) {}
  Value(long long i) : bits_(makeIntRaw(static_cast<int64_t>(i))) {}
  Value(unsigned int i) : bits_(makeIntRaw(static_cast<int64_t>(i))) {}
  Value(unsigned long long i) : bits_(makeIntRaw(static_cast<int64_t>(i))) {}

  Value(double d) {
    std::memcpy(&bits_, &d, sizeof(double));
    // If the double happens to be NaN, treat as null to avoid ambiguity
    if (isBoxed(bits_)) {
      bits_ = makeNullRaw();
    }
  }

  Value(float f) : Value(static_cast<double>(f)) {}

  // Pointer constructor
  Value(void *ptr) : bits_(makePtrRaw(ptr)) {}

  // ========================================================================
  // Factory Methods
  // ========================================================================

  static Value makeDouble(double d) {
    Value v;
    std::memcpy(&v.bits_, &d, sizeof(double));
    // NaN doubles are ambiguous, convert to null
    if (isBoxed(v.bits_)) {
      v.bits_ = makeNullRaw();
    }
    return v;
  }

  static Value makeInt(int64_t i) { return Value(makeIntRaw(i)); }

  static Value makeBool(bool b) { return Value(makeBoolRaw(b)); }

  static Value makeNull() { return Value(makeNullRaw()); }

  static Value makePtr(void *ptr) { return Value(makePtrRaw(ptr)); }

  static Value makeStringId(uint32_t id) { return Value(makeStringIdRaw(id)); }

  static Value makeObjectId(uint32_t id) { return Value(makeObjectIdRaw(id)); }

  static Value makeClosureId(uint32_t id) {
    return Value(makeExtendedRaw(static_cast<uint64_t>(ExtendedTag::CLOSURE_ID), id));
  }

  static Value makeArrayId(uint32_t id) {
    return Value(makeExtendedRaw(static_cast<uint64_t>(ExtendedTag::ARRAY_ID), id));
  }

  static Value makeSetId(uint32_t id) {
    return Value(makeExtendedRaw(static_cast<uint64_t>(ExtendedTag::SET_ID), id));
  }

  static Value makeRangeId(uint32_t id) {
    return Value(makeExtendedRaw(static_cast<uint64_t>(ExtendedTag::RANGE_ID), id));
  }

  static Value makeEnumId(uint32_t id, uint32_t typeId = 0) {
    uint64_t payload = (static_cast<uint64_t>(typeId & 0xFFF) << 32) | id;
    return Value(makeExtendedRaw(static_cast<uint64_t>(ExtendedTag::ENUM_ID), payload));
  }

  static Value makeIteratorId(uint32_t id) {
    return Value(makeExtendedRaw(static_cast<uint64_t>(ExtendedTag::ITERATOR_ID), id));
  }

  static Value makeHostFuncId(uint32_t id) {
    return Value(makeExtendedRaw(static_cast<uint64_t>(ExtendedTag::HOST_FUNC_ID), id));
  }

  static Value makeLazyPipelineId(uint32_t id) {
    return Value(makeExtendedRaw(static_cast<uint64_t>(ExtendedTag::LAZY_PIPELINE_ID), id));
  }

  static Value makeErrorId(uint32_t id) {
    return Value(makeExtendedRaw(static_cast<uint64_t>(ExtendedTag::ERROR_ID), id));
  }

  static Value makeFunctionObjId(uint32_t functionIndex) {
    return Value(makeExtendedRaw(static_cast<uint64_t>(ExtendedTag::FUNCTION_OBJ_ID), functionIndex));
  }

  static Value makeStringValId(uint32_t id) {
    return Value(makeExtendedRaw(static_cast<uint64_t>(ExtendedTag::STRING_VAL_ID), id));
  }

  // Concurrency object value types
  static Value makeThreadId(uint32_t id) {
    return Value(makeExtendedRaw(static_cast<uint64_t>(ExtendedTag::THREAD_ID), id));
  }

  static Value makeIntervalId(uint32_t id) {
    return Value(makeExtendedRaw(static_cast<uint64_t>(ExtendedTag::INTERVAL_ID), id));
  }

  static Value makeTimeoutId(uint32_t id) {
    return Value(makeExtendedRaw(static_cast<uint64_t>(ExtendedTag::TIMEOUT_ID), id));
  }

  // ========================================================================
  // Type Predicates
  // ========================================================================

  bool isDouble() const { return !isBoxed(bits_); }

  bool isInt() const {
    return isBoxed(bits_) && extractTag(bits_) == ValueTag::INT48;
  }

  bool isBool() const {
    return isBoxed(bits_) && extractTag(bits_) == ValueTag::BOOL;
  }

  bool isNull() const {
    return isBoxed(bits_) && extractTag(bits_) == ValueTag::NULL_;
  }

  bool isPtr() const {
    return isBoxed(bits_) && extractTag(bits_) == ValueTag::PTR;
  }

  bool isStringId() const {
    return isBoxed(bits_) && extractTag(bits_) == ValueTag::STRING_ID;
  }

  bool isObjectId() const {
    return isBoxed(bits_) && extractTag(bits_) == ValueTag::OBJECT_ID;
  }

  bool isClosureId() const {
    return isBoxed(bits_) && extractTag(bits_) == ValueTag::EXTENDED &&
           extractExtendedTag(bits_) == ExtendedTag::CLOSURE_ID;
  }

  bool isArrayId() const {
    return isBoxed(bits_) && extractTag(bits_) == ValueTag::EXTENDED &&
           extractExtendedTag(bits_) == ExtendedTag::ARRAY_ID;
  }

  bool isSetId() const {
    return isBoxed(bits_) && extractTag(bits_) == ValueTag::EXTENDED &&
           extractExtendedTag(bits_) == ExtendedTag::SET_ID;
  }

  bool isRangeId() const {
    return isBoxed(bits_) && extractTag(bits_) == ValueTag::EXTENDED &&
           extractExtendedTag(bits_) == ExtendedTag::RANGE_ID;
  }

  bool isEnumId() const {
    return isBoxed(bits_) && extractTag(bits_) == ValueTag::EXTENDED &&
           extractExtendedTag(bits_) == ExtendedTag::ENUM_ID;
  }

  bool isIteratorId() const {
    return isBoxed(bits_) && extractTag(bits_) == ValueTag::EXTENDED &&
           extractExtendedTag(bits_) == ExtendedTag::ITERATOR_ID;
  }

  bool isHostFuncId() const {
    return isBoxed(bits_) && extractTag(bits_) == ValueTag::EXTENDED &&
           extractExtendedTag(bits_) == ExtendedTag::HOST_FUNC_ID;
  }

  bool isLazyPipelineId() const {
    return isBoxed(bits_) && extractTag(bits_) == ValueTag::EXTENDED &&
           extractExtendedTag(bits_) == ExtendedTag::LAZY_PIPELINE_ID;
  }

  bool isErrorId() const {
    return isBoxed(bits_) && extractTag(bits_) == ValueTag::EXTENDED &&
           extractExtendedTag(bits_) == ExtendedTag::ERROR_ID;
  }

  bool isFunctionObjId() const {
    return isBoxed(bits_) && extractTag(bits_) == ValueTag::EXTENDED &&
           extractExtendedTag(bits_) == ExtendedTag::FUNCTION_OBJ_ID;
  }

  bool isStringValId() const {
    return isBoxed(bits_) && extractTag(bits_) == ValueTag::EXTENDED &&
           extractExtendedTag(bits_) == ExtendedTag::STRING_VAL_ID;
  }

  bool isThreadId() const {
    return isBoxed(bits_) && extractTag(bits_) == ValueTag::EXTENDED &&
           extractExtendedTag(bits_) == ExtendedTag::THREAD_ID;
  }

  bool isIntervalId() const {
    return isBoxed(bits_) && extractTag(bits_) == ValueTag::EXTENDED &&
           extractExtendedTag(bits_) == ExtendedTag::INTERVAL_ID;
  }

  bool isTimeoutId() const {
    return isBoxed(bits_) && extractTag(bits_) == ValueTag::EXTENDED &&
           extractExtendedTag(bits_) == ExtendedTag::TIMEOUT_ID;
  }

  // Convenience: check if numeric
  bool isNumber() const { return isDouble() || isInt(); }

  // ========================================================================
  // Getters
  // ========================================================================

  double asDouble() const {
    double d;
    std::memcpy(&d, &bits_, sizeof(double));
    return d;
  }

  int64_t asInt() const {
    uint64_t payload = extractPayload(bits_);
    return signExtend48(payload);
  }

  bool asBool() const {
    uint64_t payload = extractPayload(bits_);
    return payload != 0;
  }

  void *asPtr() const {
    uint64_t payload = extractPayload(bits_);
    // Reconstruct pointer (upper bits are zero for user-space addresses)
    return reinterpret_cast<void *>(payload);
  }

  uint32_t asStringId() const {
    return static_cast<uint32_t>(extractPayload(bits_));
  }

  uint32_t asObjectId() const {
    return static_cast<uint32_t>(extractPayload(bits_));
  }

  uint32_t asClosureId() const {
    return static_cast<uint32_t>(extractPayload(bits_));
  }

  uint32_t asArrayId() const {
    return static_cast<uint32_t>(extractPayload(bits_));
  }

  uint32_t asSetId() const {
    return static_cast<uint32_t>(extractPayload(bits_));
  }

  uint32_t asRangeId() const {
    return static_cast<uint32_t>(extractPayload(bits_));
  }

  uint32_t asEnumId() const {
    return static_cast<uint32_t>(extractPayload(bits_) & 0xFFFFFFFFULL);
  }

  uint32_t asEnumTypeId() const {
    return static_cast<uint32_t>((extractPayload(bits_) >> 32) & 0xFFFULL);
  }

  uint32_t asIteratorId() const {
    return static_cast<uint32_t>(extractPayload(bits_));
  }

  uint32_t asHostFuncId() const {
    return static_cast<uint32_t>(extractPayload(bits_));
  }

  uint32_t asLazyPipelineId() const {
    return static_cast<uint32_t>(extractPayload(bits_));
  }

  uint32_t asErrorId() const {
    return static_cast<uint32_t>(extractPayload(bits_));
  }

  uint32_t asFunctionObjId() const {
    return static_cast<uint32_t>(extractPayload(bits_));
  }

  uint32_t asStringValId() const {
    return static_cast<uint32_t>(extractPayload(bits_));
  }

  uint32_t asThreadId() const {
    return static_cast<uint32_t>(extractPayload(bits_));
  }

  uint32_t asIntervalId() const {
    return static_cast<uint32_t>(extractPayload(bits_));
  }

  uint32_t asTimeoutId() const {
    return static_cast<uint32_t>(extractPayload(bits_));
  }

  // Numeric conversion
  int64_t asInt64() const {
    if (isInt())
      return asInt();
    if (isDouble())
      return static_cast<int64_t>(asDouble());
    return 0;
  }

  double asNumber() const {
    if (isDouble())
      return asDouble();
    if (isInt())
      return static_cast<double>(asInt());
    return 0.0;
  }

  // ========================================================================
  // Raw bits access (for VM internal use)
  // ========================================================================

  uint64_t rawBits() const { return bits_; }

  // ========================================================================
  // Equality
  // ========================================================================

  bool operator==(const Value &other) const {
    // Fast path: identical bits
    if (bits_ == other.bits_)
      return true;

    // Type mismatch
    if (isDouble() != other.isDouble())
      return false;

    // Both doubles
    if (isDouble()) {
      return asDouble() == other.asDouble();
    }

    // Both boxed - compare tags first
    if (extractTag(bits_) != extractTag(other.bits_))
      return false;

    // Same tag - compare payloads
    switch (extractTag(bits_)) {
    case ValueTag::INT48:
      return asInt() == other.asInt();
    case ValueTag::BOOL:
      return asBool() == other.asBool();
    case ValueTag::NULL_:
      return true; // All nulls are equal
    case ValueTag::PTR:
      return asPtr() == other.asPtr();
    case ValueTag::STRING_ID:
      return asStringId() == other.asStringId();
    case ValueTag::OBJECT_ID:
      return asObjectId() == other.asObjectId();
    case ValueTag::EXTENDED:
      // Compare extended tags
      if (extractExtendedTag(bits_) != extractExtendedTag(other.bits_))
        return false;
      // Same extended tag - compare payloads
      return extractPayload(bits_) == extractPayload(other.bits_);
    default:
      return false;
    }
  }

  bool operator!=(const Value &other) const { return !(*this == other); }

  // ========================================================================
  // Convert to string
  // ========================================================================

  std::string toString() const;

private:
  // ========================================================================
  // Raw encoding helpers
  // ========================================================================

  static uint64_t makeTaggedRaw(uint64_t tag, uint64_t payload) {
    return QNAN | (tag << 48) | (payload & PAYLOAD_MASK);
  }

  static uint64_t makeExtendedRaw(uint64_t extendedTag, uint64_t payload) {
    // Extended values: primary tag = 7 (EXTENDED) in bits 48-50,
    // extended tag in bits 45-47, payload in bits 0-44.
    // Use 4-bit extended tag at bits 44-47 to avoid overlap with primary tag.
    // Full upper bits: (7 << 48) | (extendedTag << 44)
    return QNAN | (static_cast<uint64_t>(ValueTag::EXTENDED) << 48) |
           (extendedTag << 44) | (payload & 0x00000FFFFFFFFFFFULL);
  }

  static uint64_t makeIntRaw(int64_t i) {
    return makeTaggedRaw(static_cast<uint64_t>(ValueTag::INT48),
                         truncate48(i));
  }

  static uint64_t makeBoolRaw(bool b) {
    return makeTaggedRaw(static_cast<uint64_t>(ValueTag::BOOL),
                         b ? BOOL_TRUE : BOOL_FALSE);
  }

  static uint64_t makeNullRaw() {
    return makeTaggedRaw(static_cast<uint64_t>(ValueTag::NULL_), 0);
  }

  static uint64_t makePtrRaw(void *ptr) {
    uint64_t addr = reinterpret_cast<uint64_t>(ptr);
    return makeTaggedRaw(static_cast<uint64_t>(ValueTag::PTR),
                         addr & PAYLOAD_MASK);
  }

  static uint64_t makeStringIdRaw(uint32_t id) {
    return makeTaggedRaw(static_cast<uint64_t>(ValueTag::STRING_ID), id);
  }

  static uint64_t makeObjectIdRaw(uint32_t id) {
    return makeTaggedRaw(static_cast<uint64_t>(ValueTag::OBJECT_ID), id);
  }
};

// ============================================================================
// Utility functions
// ============================================================================

inline Value unwrapResult(const Result &result) {
  if (auto *val = std::get_if<Value>(&result)) {
    return *val;
  }
  if (auto *ret = std::get_if<ReturnValue>(&result)) {
    return ret->value ? *ret->value : Value();
  }
  if (auto *brk = std::get_if<BreakValue>(&result)) {
    (void)brk;
    return Value();
  }
  if (auto *cont = std::get_if<ContinueValue>(&result)) {
    (void)cont;
    return Value();
  }
  if (auto *err = std::get_if<RuntimeError>(&result)) {
    throw *err;
  }
  return Value();
}

inline bool isError(const Result &result) {
  return std::holds_alternative<RuntimeError>(result);
}

} // namespace havel::core
