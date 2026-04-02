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
    case OpCode::LOAD_CONST: return "LOAD_CONST";
    case OpCode::LOAD_GLOBAL: return "LOAD_GLOBAL";
    case OpCode::STORE_GLOBAL: return "STORE_GLOBAL";
    case OpCode::LOAD_VAR: return "LOAD_VAR";
    case OpCode::STORE_VAR: return "STORE_VAR";
    case OpCode::LOAD_UPVALUE: return "LOAD_UPVALUE";
    case OpCode::STORE_UPVALUE: return "STORE_UPVALUE";
    case OpCode::POP: return "POP";
    case OpCode::DUP: return "DUP";
    case OpCode::ADD: return "ADD";
    case OpCode::SUB: return "SUB";
    case OpCode::MUL: return "MUL";
    case OpCode::DIV: return "DIV";
    case OpCode::MOD: return "MOD";
    case OpCode::POW: return "POW";
    case OpCode::EQ: return "EQ";
    case OpCode::NEQ: return "NEQ";
    case OpCode::LT: return "LT";
    case OpCode::LTE: return "LTE";
    case OpCode::GT: return "GT";
    case OpCode::GTE: return "GTE";
    case OpCode::AND: return "AND";
    case OpCode::OR: return "OR";
    case OpCode::NOT: return "NOT";
    case OpCode::NEGATE: return "NEGATE";
    case OpCode::JUMP: return "JUMP";
    case OpCode::JUMP_IF_TRUE: return "JUMP_IF_TRUE";
    case OpCode::JUMP_IF_FALSE: return "JUMP_IF_FALSE";
    case OpCode::CALL: return "CALL";
    case OpCode::TAIL_CALL: return "TAIL_CALL";
    case OpCode::RETURN: return "RETURN";
    case OpCode::CLOSURE: return "CLOSURE";
    case OpCode::ARRAY_NEW: return "ARRAY_NEW";
    case OpCode::ARRAY_GET: return "ARRAY_GET";
    case OpCode::ARRAY_SET: return "ARRAY_SET";
    case OpCode::OBJECT_NEW: return "OBJECT_NEW";
    case OpCode::OBJECT_GET: return "OBJECT_GET";
    case OpCode::OBJECT_SET: return "OBJECT_SET";
    case OpCode::SPREAD: return "SPREAD";
    case OpCode::RANGE_NEW: return "RANGE_NEW";
    case OpCode::RANGE_STEP_NEW: return "RANGE_STEP_NEW";
    case OpCode::TRY_ENTER: return "TRY_ENTER";
    case OpCode::TRY_EXIT: return "TRY_EXIT";
    case OpCode::THROW: return "THROW";
    case OpCode::CALL_HOST: return "CALL_HOST";
    default: return "UNKNOWN";
  }
}

std::string BytecodeDisassembler::operandToString(const BytecodeValue& operand) {
  return std::visit([](const auto& val) -> std::string {
    using T = std::decay_t<decltype(val)>;
    if constexpr (std::is_same_v<T, int64_t>) {
      return std::to_string(val);
    } else if constexpr (std::is_same_v<T, double>) {
      return std::to_string(val);
    } else if constexpr (std::is_same_v<T, bool>) {
      return val ? "true" : "false";
    } else if constexpr (std::is_same_v<T, std::string>) {
      return "\"" + val + "\"";
    } else if constexpr (std::is_same_v<T, std::nullptr_t>) {
      return "nil";
    } else if constexpr (std::is_same_v<T, uint32_t>) {
      return std::to_string(val);
    } else {
      return "<?>"; // For FunctionObject, ClosureRef, etc.
    }
  }, operand);
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

uint32_t ConstantPool::add(const BytecodeValue& value) {
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

const BytecodeValue& ConstantPool::get(uint32_t index) const {
  if (index >= constants_.size()) {
    throw std::out_of_range("Constant index out of range");
  }
  return constants_[index];
}

bool ConstantPool::has(const BytecodeValue& value) const {
  return find(value).has_value();
}

std::optional<uint32_t> ConstantPool::find(const BytecodeValue& value) const {
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

std::string ConstantPool::hashValue(const BytecodeValue& value) const {
  return std::visit([](const auto& val) -> std::string {
    using T = std::decay_t<decltype(val)>;
    if constexpr (std::is_same_v<T, int64_t>) {
      return "i:" + std::to_string(val);
    } else if constexpr (std::is_same_v<T, double>) {
      return "d:" + std::to_string(val);
    } else if constexpr (std::is_same_v<T, bool>) {
      return val ? "b:true" : "b:false";
    } else if constexpr (std::is_same_v<T, std::string>) {
      return "s:" + val;
    } else if constexpr (std::is_same_v<T, std::nullptr_t>) {
      return "n:null";
    } else {
      return "u:ref"; // For ObjectRef, ArrayRef, etc.
    }
  }, value);
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
