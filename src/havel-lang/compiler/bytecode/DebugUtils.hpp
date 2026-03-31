#pragma once

#include <string>
#include <vector>
#include <unordered_map>
#include <cstdint>

namespace havel::compiler {

// ============================================================================
// SourceLocation - Location in source code
// ============================================================================
struct SourceLocation {
  uint32_t line = 0;
  uint32_t column = 0;
  std::string filename;

  SourceLocation() = default;
  SourceLocation(uint32_t l, uint32_t c, std::string f = "")
    : line(l), column(c), filename(std::move(f)) {}

  bool isValid() const { return line > 0; }
};

// ============================================================================
// DebugInfo - Debug information for a compiled chunk
// ============================================================================
class DebugInfo {
public:
  struct FunctionDebugInfo {
    std::string name;
    uint32_t startLine = 0;
    uint32_t endLine = 0;
    std::vector<SourceLocation> instructionLocations;
    std::vector<std::string> localNames;
    std::unordered_map<uint32_t, std::string> slotNames;
  };

  struct VariableInfo {
    std::string name;
    uint32_t slot = 0;
    uint32_t startLine = 0;
    uint32_t endLine = 0;
    bool isConst = false;
  };

  DebugInfo() = default;
  explicit DebugInfo(std::string sourceFilename);

  // Function info
  void addFunction(uint32_t functionIndex, const FunctionDebugInfo& info);
  std::optional<FunctionDebugInfo> getFunctionInfo(uint32_t index) const;

  // Line number mapping
  void mapInstructionToSource(uint32_t functionIndex,
                                uint32_t instructionIndex,
                                const SourceLocation& location);
  std::optional<SourceLocation> getSourceLocation(uint32_t functionIndex,
                                                   uint32_t instructionIndex) const;

  // Variable info
  void addVariable(uint32_t functionIndex, const VariableInfo& var);
  std::vector<VariableInfo> getVariables(uint32_t functionIndex) const;
  std::optional<VariableInfo> findVariable(uint32_t functionIndex,
                                          const std::string& name) const;

  // Serialization
  std::string serialize() const;
  bool deserialize(const std::string& data);

private:
  std::string sourceFilename_;
  std::unordered_map<uint32_t, FunctionDebugInfo> functionInfo_;
};

// ============================================================================
// SourceMap - Maps compiled code back to source locations
// ============================================================================
class SourceMap {
public:
  struct Mapping {
    uint32_t generatedLine = 0;
    uint32_t generatedColumn = 0;
    uint32_t sourceLine = 0;
    uint32_t sourceColumn = 0;
    std::string sourceFile;
    std::string symbolName;
  };

  SourceMap() = default;
  explicit SourceMap(std::string sourceRoot);

  // Add mapping
  void addMapping(const Mapping& mapping);

  // Lookup
  std::optional<Mapping> lookup(uint32_t generatedLine,
                                 uint32_t generatedColumn) const;

  // Generate VLQ-encoded source map (standard format)
  std::string generateVLQ() const;

  // Parse VLQ-encoded source map
  bool parseVLQ(const std::string& data);

  // Sources
  void addSource(const std::string& filename, const std::string& content);
  std::optional<std::string> getSourceContent(const std::string& filename) const;

private:
  std::string sourceRoot_;
  std::vector<Mapping> mappings_;
  std::unordered_map<std::string, std::string> sources_;

  // VLQ encoding helpers
  static std::string encodeVLQ(int32_t value);
  static int32_t decodeVLQ(const std::string& data, size_t& pos);
};

// ============================================================================
// VMProfiler - Performance profiling for VM execution
// ============================================================================
class VMProfiler {
public:
  struct FunctionProfile {
    std::string name;
    uint32_t callCount = 0;
    double totalTimeMs = 0.0;
    double minTimeMs = 0.0;
    double maxTimeMs = 0.0;
    uint64_t instructionsExecuted = 0;
  };

  struct OpcodeProfile {
    OpCode opcode;
    uint64_t count = 0;
    double totalTimeMs = 0.0;
  };

  struct Hotspot {
    uint32_t functionIndex = 0;
    uint32_t instructionIndex = 0;
    uint64_t executionCount = 0;
    double timeSpentMs = 0.0;
  };

  VMProfiler();
  ~VMProfiler();

  // Control
  void start();
  void stop();
  void reset();
  bool isRunning() const { return running_; }

  // Recording
  void recordFunctionEntry(uint32_t functionIndex);
  void recordFunctionExit(uint32_t functionIndex);
  void recordOpcode(OpCode opcode);
  void recordHotspot(uint32_t functionIndex, uint32_t instructionIndex);

  // Reports
  std::vector<FunctionProfile> getFunctionProfiles() const;
  std::vector<OpcodeProfile> getOpcodeProfiles() const;
  std::vector<Hotspot> getHotspots(size_t topN = 10) const;

  // Statistics
  double getTotalTimeMs() const;
  uint64_t getTotalInstructions() const;
  uint64_t getTotalFunctionCalls() const;

  // Export
  std::string generateReport() const;
  bool exportToJSON(const std::string& filename) const;

private:
  bool running_ = false;
  double startTime_ = 0.0;

  std::unordered_map<uint32_t, FunctionProfile> functionProfiles_;
  std::unordered_map<OpCode, OpcodeProfile> opcodeProfiles_;
  std::vector<Hotspot> hotspots_;

  std::unordered_map<uint32_t, double> functionStartTimes_;

  double currentTimeMs() const;
};

// ============================================================================
// BytecodeDisassembler - Disassemble bytecode for debugging
// ============================================================================
class BytecodeDisassembler {
public:
  explicit BytecodeDisassembler(const BytecodeChunk& chunk);

  // Disassembly options
  struct Options {
    bool showLineNumbers = true;
    bool showSourceLocations = true;
    bool showConstantPool = true;
    bool showFunctionInfo = true;
    bool useLabels = true;
  };

  // Main disassembly
  std::string disassemble(const Options& options = Options{}) const;
  std::string disassembleFunction(uint32_t functionIndex,
                                   const Options& options = Options{}) const;

  // Single instruction
  std::string disassembleInstruction(const Instruction& instr,
                                      uint32_t index,
                                      const Options& options = Options{}) const;

  // Constant pool
  std::string disassembleConstantPool() const;

  // Helper
  static std::string opcodeToString(OpCode opcode);
  static std::string operandToString(const BytecodeValue& operand);

private:
  const BytecodeChunk& chunk_;

  std::string formatInstruction(uint32_t index,
                                 const Instruction& instr,
                                 const Options& options) const;
};

// ============================================================================
// ConstantPool - Efficient constant deduplication
// ============================================================================
class ConstantPool {
public:
  ConstantPool() = default;

  // Add constant, returns index (existing or new)
  uint32_t add(const BytecodeValue& value);

  // Get constant by index
  const BytecodeValue& get(uint32_t index) const;

  // Check if constant exists
  bool has(const BytecodeValue& value) const;

  // Find existing constant
  std::optional<uint32_t> find(const BytecodeValue& value) const;

  // Size
  size_t size() const { return constants_.size(); }
  void clear();

  // Bulk operations
  void reserve(size_t capacity);
  std::vector<BytecodeValue> getAll() const { return constants_; }

  // Serialization
  std::string serialize() const;
  bool deserialize(const std::string& data);

private:
  std::vector<BytecodeValue> constants_;
  std::unordered_map<std::string, uint32_t> indexMap_; // Hash -> index

  std::string hashValue(const BytecodeValue& value) const;
};

} // namespace havel::compiler
