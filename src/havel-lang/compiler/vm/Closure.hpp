#pragma once

#include "../core/BytecodeIR.hpp"
#include "../gc/GC.hpp"
#include <cstdint>
#include <memory>
#include <vector>

namespace havel::compiler {

// Forward declarations
class GCHeap;

// ============================================================================
// Upvalue - Represents a captured variable from an outer scope
// ============================================================================
class Upvalue {
public:
  // Types of upvalue capture
  enum class Type : uint8_t {
    Local,      // Captures a local variable from outer function
    Upvalue     // Captures an upvalue from outer function
  };

  Upvalue(uint32_t sourceIndex, Type type, bool isConst = false);

  // Accessors
  uint32_t getSourceIndex() const { return sourceIndex_; }
  Type getType() const { return type_; }
  bool isConst() const { return isConst_; }

  // GC cell management
  void setGCTarget(std::shared_ptr<GCHeap::UpvalueCell> cell) { gcCell_ = cell; }
  std::shared_ptr<GCHeap::UpvalueCell> getGCTarget() const { return gcCell_; }
  bool hasGCTarget() const { return gcCell_ != nullptr; }

  // Value access (delegates to GC cell if present)
  BytecodeValue getValue() const;
  void setValue(const BytecodeValue& value);

  // Close the upvalue - captures the current value
  void close(const BytecodeValue& value);
  bool isClosed() const;

private:
  uint32_t sourceIndex_;
  Type type_;
  bool isConst_;
  std::shared_ptr<GCHeap::UpvalueCell> gcCell_;
};

// ============================================================================
// Closure - Represents a function with captured environment
// ============================================================================
class Closure {
public:
  Closure(uint32_t functionIndex, std::vector<Upvalue> upvalues);

  // Factory method for creating closures with runtime upvalue capture
  static Closure create(uint32_t functionIndex,
                        const std::vector<UpvalueDescriptor>& descriptors,
                        const std::vector<std::shared_ptr<GCHeap::UpvalueCell>>&
                            parentUpvalues,
                        const std::vector<BytecodeValue>& locals);

  // Accessors
  uint32_t getFunctionIndex() const { return functionIndex_; }
  const std::vector<Upvalue>& getUpvalues() const { return upvalues_; }
  std::vector<Upvalue>& getUpvalues() { return upvalues_; }
  size_t getUpvalueCount() const { return upvalues_.size(); }

  // Upvalue access
  Upvalue& getUpvalue(size_t index);
  const Upvalue& getUpvalue(size_t index) const;
  BytecodeValue getUpvalueValue(size_t index) const;
  void setUpvalueValue(size_t index, const BytecodeValue& value);

  // Check if this closure has a specific upvalue by source index
  bool hasUpvalue(uint32_t sourceIndex) const;
  std::optional<size_t> findUpvalueIndex(uint32_t sourceIndex) const;

  // Convert to BytecodeValue (ClosureRef)
  BytecodeValue toValue(uint32_t closureId) const;

  // Validation
  bool isValid() const { return functionIndex_ != UINT32_MAX; }

private:
  uint32_t functionIndex_;
  std::vector<Upvalue> upvalues_;
};

// ============================================================================
// ClosureFactory - Helper for creating closures during compilation/execution
// ============================================================================
class ClosureFactory {
public:
  static Closure createFromFunction(uint32_t functionIndex);
  static Closure createWithUpvalues(
      uint32_t functionIndex,
      const std::vector<UpvalueDescriptor>& descriptors);
};

// ============================================================================
// UpvalueManager - Manages open upvalues during execution
// ============================================================================
class UpvalueManager {
public:
  explicit UpvalueManager(GCHeap& heap);

  // Open a new upvalue or return existing one
  std::shared_ptr<GCHeap::UpvalueCell> openUpvalue(uint32_t localIndex,
                                                    const BytecodeValue& value);

  // Close upvalues for a range of locals (when function returns)
  void closeUpvalues(uint32_t localsBase, uint32_t localsEnd,
                       const std::vector<BytecodeValue>& locals);

  // Close all upvalues
  void closeAllUpvalues(const std::vector<BytecodeValue>& locals);

  // Get existing open upvalue if present
  std::shared_ptr<GCHeap::UpvalueCell> getOpenUpvalue(uint32_t localIndex) const;

  // Check if an upvalue is open
  bool isUpvalueOpen(uint32_t localIndex) const;

  // Get count of open upvalues
  size_t getOpenCount() const { return openUpvalues_.size(); }

  // Clear all open upvalues (for cleanup)
  void clear();

private:
  GCHeap& heap_;
  std::unordered_map<uint32_t, std::shared_ptr<GCHeap::UpvalueCell>> openUpvalues_;
};

} // namespace havel::compiler
