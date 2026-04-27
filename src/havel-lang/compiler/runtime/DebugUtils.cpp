#include "DebugUtils.hpp"
#include "../core/BytecodeIR.hpp"
#include <sstream>
#include <iomanip>
#include <chrono>

namespace havel::compiler {

// ============================================================================
// DebugInfo Implementation
// ============================================================================

DebugInfo::DebugInfo(std::string sourceFilename)
    : sourceFilename_(std::move(sourceFilename)) {}

void DebugInfo::addFunction(uint32_t functionIndex, const FunctionDebugInfo& info) {
  functionInfo_[functionIndex] = info;
}

std::optional<DebugInfo::FunctionDebugInfo> DebugInfo::getFunctionInfo(
    uint32_t index) const {
  auto it = functionInfo_.find(index);
  if (it != functionInfo_.end()) {
    return it->second;
  }
  return std::nullopt;
}

void DebugInfo::mapInstructionToSource(uint32_t functionIndex,
                                        uint32_t instructionIndex,
                                        const SourceLocation& location) {
  auto it = functionInfo_.find(functionIndex);
  if (it != functionInfo_.end()) {
    if (instructionIndex >= it->second.instructionLocations.size()) {
      it->second.instructionLocations.resize(instructionIndex + 1);
    }
    it->second.instructionLocations[instructionIndex] = location;
  }
}

std::optional<SourceLocation> DebugInfo::getSourceLocation(
    uint32_t functionIndex,
    uint32_t instructionIndex) const {
  auto it = functionInfo_.find(functionIndex);
  if (it != functionInfo_.end() &&
      instructionIndex < it->second.instructionLocations.size()) {
    return it->second.instructionLocations[instructionIndex];
  }
  return std::nullopt;
}

void DebugInfo::addVariable(uint32_t functionIndex, const VariableInfo& var) {
  auto it = functionInfo_.find(functionIndex);
  if (it != functionInfo_.end()) {
    // Could store in a more structured way
    (void)var;
  }
}

std::vector<DebugInfo::VariableInfo> DebugInfo::getVariables(
    uint32_t functionIndex) const {
  (void)functionIndex;
  return {};
}

std::optional<DebugInfo::VariableInfo> DebugInfo::findVariable(
    uint32_t functionIndex,
    const std::string& name) const {
  (void)functionIndex;
  (void)name;
  return std::nullopt;
}

std::string DebugInfo::serialize() const {
  // Simple serialization format (could be JSON)
  std::stringstream ss;
  ss << "DEBUGINFO v1\n";
  ss << "source: " << sourceFilename_ << "\n";
  ss << "functions: " << functionInfo_.size() << "\n";

  for (const auto& [index, info] : functionInfo_) {
    ss << "func " << index << " " << info.name << "\n";
    ss << "  lines: " << info.startLine << "-" << info.endLine << "\n";
    ss << "  instructions: " << info.instructionLocations.size() << "\n";
  }

  return ss.str();
}

bool DebugInfo::deserialize(const std::string& data) {
  // Simple deserialization (could parse JSON)
  (void)data;
  return false; // Not fully implemented
}

// ============================================================================
// SourceMap Implementation
// ============================================================================

SourceMap::SourceMap(std::string sourceRoot) : sourceRoot_(std::move(sourceRoot)) {}

void SourceMap::addMapping(const Mapping& mapping) {
  mappings_.push_back(mapping);
}

std::optional<SourceMap::Mapping> SourceMap::lookup(uint32_t generatedLine,
                                                     uint32_t generatedColumn) const {
  // Find closest mapping
  const Mapping* bestMatch = nullptr;
  uint32_t bestDistance = UINT32_MAX;

  for (const auto& mapping : mappings_) {
    if (mapping.generatedLine == generatedLine) {
      uint32_t distance = (generatedColumn > mapping.generatedColumn) ?
                          (generatedColumn - mapping.generatedColumn) :
                          (mapping.generatedColumn - generatedColumn);
      if (distance < bestDistance) {
        bestDistance = distance;
        bestMatch = &mapping;
      }
    }
  }

  if (bestMatch) {
    return *bestMatch;
  }
  return std::nullopt;
}

std::string SourceMap::generateVLQ() const {
  // Simplified VLQ generation (not full spec compliant)
  std::stringstream ss;
  ss << "{\n";
  ss << "  \"version\": 3,\n";
  ss << "  \"sourceRoot\": \"" << sourceRoot_ << "\",\n";
  ss << "  \"sources\": [";

  bool first = true;
  for (const auto& [filename, _] : sources_) {
    (void)_;
    if (!first) ss << ", ";
    ss << "\"" << filename << "\"";
    first = false;
  }
  ss << "],\n";

  ss << "  \"mappings\": \"";
  // Mappings would be VLQ encoded here
  ss << "\"\n";
  ss << "}\n";

  return ss.str();
}

bool SourceMap::parseVLQ(const std::string& data) {
  (void)data;
  return false; // Not fully implemented
}

void SourceMap::addSource(const std::string& filename, const std::string& content) {
  sources_[filename] = content;
}

std::optional<std::string> SourceMap::getSourceContent(const std::string& filename) const {
  auto it = sources_.find(filename);
  if (it != sources_.end()) {
    return it->second;
  }
  return std::nullopt;
}

std::string SourceMap::encodeVLQ(int32_t value) {
  // Simplified VLQ encoding
  std::string result;
  bool negative = value < 0;
  uint32_t vlq = negative ? (-value) << 1 : value << 1;
  if (negative) vlq |= 1;

  do {
    uint32_t digit = vlq & 0x1f;
    vlq >>= 5;
    if (vlq > 0) digit |= 0x20;
    result += static_cast<char>('A' + digit); // Simplified
  } while (vlq > 0);

  return result;
}

int32_t SourceMap::decodeVLQ(const std::string& data, size_t& pos) {
  // Simplified VLQ decoding
  int32_t result = 0;
  int shift = 0;

  while (pos < data.size()) {
    char c = data[pos++];
    int32_t digit = (c - 'A'); // Simplified
    result |= (digit & 0x1f) << shift;
    if ((digit & 0x20) == 0) break;
    shift += 5;
  }

  bool negative = (result & 1) != 0;
  result >>= 1;
  return negative ? -result : result;
}

// ============================================================================
// VMProfiler Implementation
// ============================================================================

VMProfiler::VMProfiler() = default;

VMProfiler::~VMProfiler() {
  if (running_) {
    stop();
  }
}

void VMProfiler::start() {
  running_ = true;
  startTime_ = currentTimeMs();
}

void VMProfiler::stop() {
  running_ = false;
}

void VMProfiler::reset() {
  functionProfiles_.clear();
  opcodeProfiles_.clear();
  hotspots_.clear();
  functionStartTimes_.clear();
}

void VMProfiler::recordFunctionEntry(uint32_t functionIndex) {
  if (!running_) return;

  functionStartTimes_[functionIndex] = currentTimeMs();

  auto& profile = functionProfiles_[functionIndex];
  profile.callCount++;
}

void VMProfiler::recordFunctionExit(uint32_t functionIndex) {
  if (!running_) return;

  auto it = functionStartTimes_.find(functionIndex);
  if (it != functionStartTimes_.end()) {
    double elapsed = currentTimeMs() - it->second;
    auto& profile = functionProfiles_[functionIndex];

    profile.totalTimeMs += elapsed;
    if (profile.minTimeMs == 0 || elapsed < profile.minTimeMs) {
      profile.minTimeMs = elapsed;
    }
    if (elapsed > profile.maxTimeMs) {
      profile.maxTimeMs = elapsed;
    }

    functionStartTimes_.erase(it);
  }
}

void VMProfiler::recordOpcode(OpCode opcode) {
  if (!running_) return;

  auto& profile = opcodeProfiles_[opcode];
  profile.opcode = opcode;
  profile.count++;
}

void VMProfiler::recordHotspot(uint32_t functionIndex, uint32_t instructionIndex) {
  if (!running_) return;

  // Simplified - just add to list
  Hotspot spot;
  spot.functionIndex = functionIndex;
  spot.instructionIndex = instructionIndex;
  spot.executionCount = 1;
  hotspots_.push_back(spot);
}

std::vector<VMProfiler::FunctionProfile> VMProfiler::getFunctionProfiles() const {
  std::vector<FunctionProfile> result;
  for (const auto& [_, profile] : functionProfiles_) {
    (void)_;
    result.push_back(profile);
  }
  return result;
}

std::vector<VMProfiler::OpcodeProfile> VMProfiler::getOpcodeProfiles() const {
  std::vector<OpcodeProfile> result;
  for (const auto& [_, profile] : opcodeProfiles_) {
    (void)_;
    result.push_back(profile);
  }
  return result;
}

std::vector<VMProfiler::Hotspot> VMProfiler::getHotspots(size_t topN) const {
  // Sort by execution count
  std::vector<Hotspot> result = hotspots_;
  std::sort(result.begin(), result.end(),
            [](const Hotspot& a, const Hotspot& b) {
              return a.executionCount > b.executionCount;
            });

  if (result.size() > topN) {
    result.resize(topN);
  }
  return result;
}

double VMProfiler::getTotalTimeMs() const {
  if (!running_) return 0.0;
  return currentTimeMs() - startTime_;
}

uint64_t VMProfiler::getTotalInstructions() const {
  uint64_t total = 0;
  for (const auto& [_, profile] : opcodeProfiles_) {
    (void)_;
    total += profile.count;
  }
  return total;
}

uint64_t VMProfiler::getTotalFunctionCalls() const {
  uint64_t total = 0;
  for (const auto& [_, profile] : functionProfiles_) {
    (void)_;
    total += profile.callCount;
  }
  return total;
}

std::string VMProfiler::generateReport() const {
  std::stringstream ss;
  ss << "=== VM Profile Report ===\n\n";

  ss << "Total time: " << getTotalTimeMs() << " ms\n";
  ss << "Total instructions: " << getTotalInstructions() << "\n";
  ss << "Total function calls: " << getTotalFunctionCalls() << "\n\n";

  ss << "Function Profiles:\n";
  auto funcProfiles = getFunctionProfiles();
  std::sort(funcProfiles.begin(), funcProfiles.end(),
            [](const FunctionProfile& a, const FunctionProfile& b) {
              return a.totalTimeMs > b.totalTimeMs;
            });

  for (const auto& profile : funcProfiles) {
    ss << "  " << profile.name << ":\n";
    ss << "    calls: " << profile.callCount << "\n";
    ss << "    total: " << profile.totalTimeMs << " ms\n";
    ss << "    avg: " << (profile.callCount > 0 ?
                          profile.totalTimeMs / profile.callCount : 0) << " ms\n";
    ss << "    min: " << profile.minTimeMs << " ms\n";
    ss << "    max: " << profile.maxTimeMs << " ms\n";
  }

  return ss.str();
}

bool VMProfiler::exportToJSON(const std::string& filename) const {
  (void)filename;
  // Not fully implemented
  return false;
}

double VMProfiler::currentTimeMs() const {
  using namespace std::chrono;
  auto now = steady_clock::now().time_since_epoch();
  return duration<double, std::milli>(now).count();
}

// ============================================================================
// BytecodeDisassembler Implementation
// ============================================================================

BytecodeDisassembler::BytecodeDisassembler(const BytecodeChunk& chunk) : chunk_(chunk) {}

std::string BytecodeDisassembler::disassemble(const Options& options) const {
  std::stringstream ss;

  if (options.showConstantPool) {
    ss << disassembleConstantPool();
    ss << "\n";
  }

  for (size_t i = 0; i < chunk_.getFunctionCount(); ++i) {
    if (i > 0) ss << "\n";
    ss << disassembleFunction(static_cast<uint32_t>(i), options);
  }

  return ss.str();
}

std::string BytecodeDisassembler::disassembleFunction(
    uint32_t functionIndex,
    const Options& options) const {
  auto* function = chunk_.getFunction(functionIndex);
  if (!function) {
    return "Invalid function index\n";
  }

  std::stringstream ss;

  if (options.showFunctionInfo) {
    ss << "=== Function " << functionIndex << " ===\n";
    ss << "Name: " << function->name << "\n";
    ss << "Params: " << function->param_count << "\n";
    ss << "Instructions: " << function->instructions.size() << "\n";
  }

  ss << "\n";

  for (size_t i = 0; i < function->instructions.size(); ++i) {
    if (options.showLineNumbers) {
      ss << std::setw(4) << i << ": ";
    }
    ss << disassembleInstruction(function->instructions[i],
                                  static_cast<uint32_t>(i),
                                  options);
    ss << "\n";
  }

  return ss.str();
}

std::string BytecodeDisassembler::disassembleInstruction(
    const Instruction& instr,
    uint32_t index,
    const Options& options) const {
  (void)index;
  std::stringstream ss;

  ss << "  " << std::left << std::setw(15) << opcodeToString(instr.opcode);

  for (const auto& operand : instr.operands) {
    ss << " " << operandToString(operand);
  }

  if (options.showSourceLocations && instr.location) {
    ss << "  ; " << instr.location->line << ":" << instr.location->column;
  }

  return ss.str();
}

std::string BytecodeDisassembler::disassembleConstantPool() const {
  std::stringstream ss;
  ss << "=== Constant Pool ===\n";
  ss << "(Constants are now function-level)\n";
  return ss.str();
}

std::string BytecodeDisassembler::opcodeToString(OpCode opcode) {
  switch (opcode) {
  // Stack operations
  case OpCode::LOAD_CONST: return "LOAD_CONST";
  case OpCode::LOAD_GLOBAL: return "LOAD_GLOBAL";
  case OpCode::STORE_GLOBAL: return "STORE_GLOBAL";
  case OpCode::LOAD_VAR: return "LOAD_VAR";
  case OpCode::STORE_VAR: return "STORE_VAR";
  case OpCode::LOAD_UPVALUE: return "LOAD_UPVALUE";
  case OpCode::STORE_UPVALUE: return "STORE_UPVALUE";
  case OpCode::POP: return "POP";
  case OpCode::DUP: return "DUP";
  case OpCode::SWAP: return "SWAP";
  case OpCode::PUSH_NULL: return "PUSH_NULL";

  // Arithmetic operations
  case OpCode::ADD: return "ADD";
  case OpCode::SUB: return "SUB";
  case OpCode::MUL: return "MUL";
  case OpCode::DIV: return "DIV";
  case OpCode::MOD: return "MOD";
  case OpCode::POW: return "POW";

  // Increment/Decrement
  case OpCode::INCLOCAL: return "INCLOCAL";
  case OpCode::DECLOCAL: return "DECLOCAL";
  case OpCode::INCLOCAL_POST: return "INCLOCAL_POST";
  case OpCode::DECLOCAL_POST: return "DECLOCAL_POST";

  // Comparison operations
  case OpCode::EQ: return "EQ";
  case OpCode::NEQ: return "NEQ";
  case OpCode::IS: return "IS";
  case OpCode::LT: return "LT";
  case OpCode::LTE: return "LTE";
  case OpCode::GT: return "GT";
  case OpCode::GTE: return "GTE";

  // Logical operations
  case OpCode::AND: return "AND";
  case OpCode::OR: return "OR";
  case OpCode::NOT: return "NOT";
  case OpCode::NEGATE: return "NEGATE";
  case OpCode::IS_NULL: return "IS_NULL";

  // Control flow
  case OpCode::JUMP: return "JUMP";
  case OpCode::JUMP_IF_FALSE: return "JUMP_IF_FALSE";
  case OpCode::JUMP_IF_TRUE: return "JUMP_IF_TRUE";
  case OpCode::JUMP_IF_NULL: return "JUMP_IF_NULL";
  case OpCode::CALL: return "CALL";
	case OpCode::TAIL_CALL: return "TAIL_CALL";
	case OpCode::CALL_METHOD: return "CALL_METHOD";
  case OpCode::RETURN: return "RETURN";
  case OpCode::TRY_ENTER: return "TRY_ENTER";
  case OpCode::TRY_EXIT: return "TRY_EXIT";
  case OpCode::LOAD_EXCEPTION: return "LOAD_EXCEPTION";
  case OpCode::THROW: return "THROW";

  // Function operations
  case OpCode::DEFINE_FUNC: return "DEFINE_FUNC";
  case OpCode::CLOSURE: return "CLOSURE";

  // Array operations
  case OpCode::ARRAY_NEW: return "ARRAY_NEW";
  case OpCode::ARRAY_GET: return "ARRAY_GET";
  case OpCode::ARRAY_SET: return "ARRAY_SET";
  case OpCode::ARRAY_DEL: return "ARRAY_DEL";
  case OpCode::ARRAY_PUSH: return "ARRAY_PUSH";
  case OpCode::ARRAY_LEN: return "ARRAY_LEN";
  case OpCode::ARRAY_FREEZE: return "ARRAY_FREEZE";

  // Set operations
  case OpCode::SET_SET: return "SET_SET";
  case OpCode::SET_DEL: return "SET_DEL";

  // Range operations
  case OpCode::RANGE_NEW: return "RANGE_NEW";
  case OpCode::RANGE_STEP_NEW: return "RANGE_STEP_NEW";

  // Enum operations
  case OpCode::ENUM_NEW: return "ENUM_NEW";
  case OpCode::ENUM_TAG: return "ENUM_TAG";
  case OpCode::ENUM_PAYLOAD: return "ENUM_PAYLOAD";
  case OpCode::ENUM_MATCH: return "ENUM_MATCH";

  // Object intrinsics
  case OpCode::OBJECT_KEYS: return "OBJECT_KEYS";
  case OpCode::OBJECT_VALUES: return "OBJECT_VALUES";
  case OpCode::OBJECT_ENTRIES: return "OBJECT_ENTRIES";
  case OpCode::OBJECT_HAS: return "OBJECT_HAS";
  case OpCode::OBJECT_DELETE: return "OBJECT_DELETE";
  case OpCode::OBJECT_GET_RAW: return "OBJECT_GET_RAW";

  // Array intrinsics
  case OpCode::ARRAY_POP: return "ARRAY_POP";
  case OpCode::ARRAY_HAS: return "ARRAY_HAS";
  case OpCode::ARRAY_FIND: return "ARRAY_FIND";
  case OpCode::ARRAY_MAP: return "ARRAY_MAP";
  case OpCode::ARRAY_FILTER: return "ARRAY_FILTER";
  case OpCode::ARRAY_REDUCE: return "ARRAY_REDUCE";
  case OpCode::ARRAY_FOREACH: return "ARRAY_FOREACH";

  // String intrinsics
  case OpCode::STRING_LEN: return "STRING_LEN";
  case OpCode::STRING_UPPER: return "STRING_UPPER";
  case OpCode::STRING_LOWER: return "STRING_LOWER";
  case OpCode::STRING_TRIM: return "STRING_TRIM";
  case OpCode::STRING_SUB: return "STRING_SUB";
  case OpCode::STRING_FIND: return "STRING_FIND";
  case OpCode::STRING_HAS: return "STRING_HAS";
  case OpCode::STRING_STARTS: return "STRING_STARTS";
  case OpCode::STRING_ENDS: return "STRING_ENDS";
  case OpCode::STRING_SPLIT: return "STRING_SPLIT";
  case OpCode::STRING_REPLACE: return "STRING_REPLACE";
  case OpCode::STRING_PROMOTE: return "STRING_PROMOTE";

  // Iteration protocol
  case OpCode::ITER_NEW: return "ITER_NEW";
  case OpCode::ITER_NEXT: return "ITER_NEXT";
  case OpCode::SET_NEW: return "SET_NEW";

  // Object operations
  case OpCode::OBJECT_NEW: return "OBJECT_NEW";
  case OpCode::OBJECT_NEW_UNSORTED: return "OBJECT_NEW_UNSORTED";
  case OpCode::OBJECT_GET: return "OBJECT_GET";
  case OpCode::OBJECT_SET: return "OBJECT_SET";

  // String operations
  case OpCode::STRING_CONCAT: return "STRING_CONCAT";

  // Spread operator
  case OpCode::SPREAD: return "SPREAD";
  case OpCode::SPREAD_CALL: return "SPREAD_CALL";

  // Type conversion
  case OpCode::AS_TYPE: return "AS_TYPE";
  case OpCode::TO_INT: return "TO_INT";
  case OpCode::TO_FLOAT: return "TO_FLOAT";
  case OpCode::TO_STRING: return "TO_STRING";
  case OpCode::TO_BOOL: return "TO_BOOL";
  case OpCode::TYPE_OF: return "TYPE_OF";

  // Special operations
  case OpCode::PRINT: return "PRINT";
  case OpCode::DEBUG: return "DEBUG";

  // Class operations
  case OpCode::CLASS_NEW: return "CLASS_NEW";
  case OpCode::CLASS_GET_FIELD: return "CLASS_GET_FIELD";
  case OpCode::CLASS_SET_FIELD: return "CLASS_SET_FIELD";
  case OpCode::LOAD_CLASS_PROTO: return "LOAD_CLASS_PROTO";
  case OpCode::CALL_SUPER: return "CALL_SUPER";
  case OpCode::IMPORT: return "IMPORT";

  // Concurrency primitives
  case OpCode::THREAD_SPAWN: return "THREAD_SPAWN";
  case OpCode::THREAD_JOIN: return "THREAD_JOIN";
  case OpCode::THREAD_SEND: return "THREAD_SEND";
  case OpCode::THREAD_RECEIVE: return "THREAD_RECEIVE";
  case OpCode::INTERVAL_START: return "INTERVAL_START";
  case OpCode::INTERVAL_STOP: return "INTERVAL_STOP";
  case OpCode::TIMEOUT_START: return "TIMEOUT_START";
  case OpCode::TIMEOUT_CANCEL: return "TIMEOUT_CANCEL";

  // Coroutines
  case OpCode::YIELD: return "YIELD";
  case OpCode::YIELD_RESUME: return "YIELD_RESUME";
  case OpCode::GO_ASYNC: return "GO_ASYNC";

  // Channels
  case OpCode::CHANNEL_NEW: return "CHANNEL_NEW";
  case OpCode::CHANNEL_SEND: return "CHANNEL_SEND";
  case OpCode::CHANNEL_RECEIVE: return "CHANNEL_RECEIVE";
  case OpCode::CHANNEL_CLOSE: return "CHANNEL_CLOSE";

  // Module context
  case OpCode::BEGIN_MODULE: return "BEGIN_MODULE";
  case OpCode::END_MODULE: return "END_MODULE";

  case OpCode::NOP: return "NOP";
  default: return "UNKNOWN";
  }
}

std::string BytecodeDisassembler::operandToString(const Value& operand) {
  if (operand.isNull()) return "nil";
  if (operand.isBool()) return operand.asBool() ? "true" : "false";
  if (operand.isInt()) return std::to_string(operand.asInt());
  if (operand.isDouble()) return std::to_string(operand.asDouble());
  if (operand.isStringValId()) return "str[" + std::to_string(operand.asStringValId()) + "]";
  if (operand.isStringId()) return "strid[" + std::to_string(operand.asStringId()) + "]";
  if (operand.isFunctionObjId()) return "fn[" + std::to_string(operand.asFunctionObjId()) + "]";
  if (operand.isClosureId()) return "closure[" + std::to_string(operand.asClosureId()) + "]";
  if (operand.isArrayId()) return "array[" + std::to_string(operand.asArrayId()) + "]";
  if (operand.isObjectId()) return "object[" + std::to_string(operand.asObjectId()) + "]";
  if (operand.isSetId()) return "set[" + std::to_string(operand.asSetId()) + "]";
  if (operand.isHostFuncId()) return "hostfn[" + std::to_string(operand.asHostFuncId()) + "]";
  if (operand.isRangeId()) return "range[" + std::to_string(operand.asRangeId()) + "]";
  if (operand.isEnumId()) return "enum[" + std::to_string(operand.asEnumId()) + "]";
  if (operand.isIteratorId()) return "iter[" + std::to_string(operand.asIteratorId()) + "]";
  return "<?>";
}

std::string BytecodeDisassembler::formatInstruction(
    uint32_t index,
    const Instruction& instr,
    const Options& options) const {
  (void)index;
  (void)instr;
  (void)options;
  return ""; // Not implemented
}

// ============================================================================
// ConstantPool Implementation
// ============================================================================

uint32_t ConstantPool::add(const Value& value) {
  // Check if already exists
  auto existing = find(value);
  if (existing) {
    return *existing;
  }

  // Add new constant
  uint32_t index = static_cast<uint32_t>(constants_.size());
  constants_.push_back(value);
  indexMap_[hashValue(value)] = index;
  return index;
}

const Value& ConstantPool::get(uint32_t index) const {
  if (index >= constants_.size()) {
    throw std::out_of_range("Constant index out of range");
  }
  return constants_[index];
}

bool ConstantPool::has(const Value& value) const {
  return find(value).has_value();
}

std::optional<uint32_t> ConstantPool::find(const Value& value) const {
  auto it = indexMap_.find(hashValue(value));
  if (it != indexMap_.end()) {
    return it->second;
  }
  return std::nullopt;
}

void ConstantPool::clear() {
  constants_.clear();
  indexMap_.clear();
}

void ConstantPool::reserve(size_t capacity) {
  constants_.reserve(capacity);
}

std::string ConstantPool::hashValue(const Value& value) const {
  if (value.isInt()) return "i:" + std::to_string(value.asInt());
  if (value.isDouble()) return "d:" + std::to_string(value.asDouble());
  if (value.isBool()) return value.asBool() ? "b:true" : "b:false";
  if (value.isNull()) return "n:null";
  if (value.isStringValId()) {
    // TODO: string pool lookup
    return "s:<string>";
  }
  return "u:ref"; // For ObjectRef, ArrayRef, etc.
}

std::string ConstantPool::serialize() const {
  std::stringstream ss;
  ss << constants_.size() << "\n";
  for (const auto& constant : constants_) {
    ss << BytecodeDisassembler::operandToString(constant) << "\n";
  }
  return ss.str();
}

bool ConstantPool::deserialize(const std::string& data) {
  (void)data;
  return false; // Not fully implemented
}

} // namespace havel::compiler
