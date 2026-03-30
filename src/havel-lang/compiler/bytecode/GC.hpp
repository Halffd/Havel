#pragma once

#include "BytecodeIR.hpp"

#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace havel::compiler {

class GCHeap {
public:
  friend class VM; // VM needs access to internal storage for struct/enum ops
  struct Stats {
    uint64_t heap_size = 0;
    uint64_t object_count = 0;
    uint64_t collections = 0;
    uint64_t last_pause_ns = 0;
  };

  struct UpvalueCell {
    bool is_open = false;
    uint32_t open_index = 0;
    BytecodeValue closed_value = nullptr;
  };

  struct RuntimeClosure {
    uint32_t function_index = 0;
    std::vector<std::shared_ptr<UpvalueCell>> upvalues;
  };

  // Iterator for iteration protocol
  struct Iterator {
    BytecodeValue
        iterable;     // The original iterable (array, string, object, range)
    size_t index = 0; // Current position
    std::vector<std::string> keys; // For object iteration
  };

  // Range for range-based iteration
  struct Range {
    int64_t start = 0;
    int64_t end = 0;
    int64_t step = 1;
  };

  // Struct type info (shared among instances)
  struct StructType {
    std::string name;
    std::vector<std::string> fieldNames;
  };

  // Class type info (shared among instances)
  struct ClassType {
    std::string name;
    std::vector<std::string> fieldNames;
    uint32_t parentTypeId = 0; // Parent class type ID (0 for none)
    std::unordered_map<std::string, uint32_t>
        methodIndices; // Method name -> function index
  };

  // Enum type info (shared among instances)
  struct EnumType {
    std::string name;
    std::vector<std::string> variantNames;
  };

  // Error object for custom error types with stack traces
  struct ErrorObject {
    std::string errorType; // e.g., "TypeError", "RangeError", "CustomError"
    std::string message;
    std::string stackTrace;
    uint32_t line = 0;
    uint32_t column = 0;
    BytecodeValue cause; // Optional chained error

    ErrorObject() = default;
    ErrorObject(const std::string &type, const std::string &msg,
                const std::string &trace = "", uint32_t ln = 0,
                uint32_t col = 0)
        : errorType(type), message(msg), stackTrace(trace), line(ln),
          column(col), cause(nullptr) {}
  };

  void reset();

  ClosureRef allocateClosure(RuntimeClosure closure);
  ArrayRef allocateArray();
  ObjectRef allocateObject();
  SetRef allocateSet();
  RangeRef allocateRange(int64_t start, int64_t end, int64_t step);
  ErrorRef allocateError(const std::string &errorType,
                         const std::string &message,
                         const std::string &stackTrace = "", uint32_t line = 0,
                         uint32_t column = 0);

  // Struct operations
  uint32_t registerStructType(const std::string &name,
                              const std::vector<std::string> &fields);
  StructRef allocateStruct(uint32_t typeId, size_t fieldCount);
  std::optional<uint32_t> findStructTypeId(const std::string &name) const;
  size_t structFieldCount(uint32_t typeId) const;
  std::optional<size_t> structFieldIndex(uint32_t typeId,
                                         const std::string &field) const;

  // Class operations
  uint32_t registerClassType(const std::string &name,
                             const std::vector<std::string> &fields,
                             uint32_t parentTypeId = 0);
  ClassRef allocateClass(uint32_t typeId, size_t fieldCount,
                         uint32_t parentInstanceId = 0);
  std::optional<uint32_t> findClassTypeId(const std::string &name) const;
  size_t classFieldCount(uint32_t typeId) const;
  std::optional<size_t> classFieldIndex(uint32_t typeId,
                                        const std::string &field) const;
  uint32_t getClassParentTypeId(uint32_t typeId) const;
  void registerClassMethod(uint32_t typeId, const std::string &methodName,
                           uint32_t functionIndex);
  std::optional<uint32_t> findClassMethod(uint32_t typeId,
                                          const std::string &methodName) const;

  // Enum operations
  uint32_t registerEnumType(const std::string &name,
                            const std::vector<std::string> &variants);
  EnumRef allocateEnum(uint32_t typeId, uint32_t tag, size_t payloadCount);

  IteratorRef allocateIterator(const BytecodeValue &iterable);

  RuntimeClosure *closure(uint32_t id);
  const RuntimeClosure *closure(uint32_t id) const;
  std::vector<BytecodeValue> *array(uint32_t id);
  std::unordered_map<std::string, BytecodeValue> *object(uint32_t id);
  std::unordered_map<std::string, BytecodeValue> *set(uint32_t id);
  Range *range(uint32_t id);
  const Range *range(uint32_t id) const;
  Iterator *iterator(uint32_t id);
  const Iterator *iterator(uint32_t id) const;

  // Error accessors
  ErrorObject *error(uint32_t id);
  const ErrorObject *error(uint32_t id) const;

  // Iteration protocol
  uint32_t createIterator(const BytecodeValue &iterable);
  BytecodeValue iteratorNext(uint32_t id);

  void setAllocationBudget(size_t value) { allocation_budget_ = value; }

  uint64_t pinExternalRoot(const BytecodeValue &value);
  bool unpinExternalRoot(uint64_t root_id);
  std::optional<BytecodeValue> externalRoot(uint64_t root_id) const;
  size_t externalRootCount() const { return external_roots_.size(); }
  Stats stats() const;

  void maybeCollectGarbage(
      const std::vector<BytecodeValue> &stack_values,
      const std::vector<BytecodeValue> &locals,
      const std::unordered_map<std::string, BytecodeValue> &globals,
      const std::vector<uint32_t> &active_closure_ids,
      const std::function<std::optional<BytecodeValue>(uint32_t)>
          &open_local_reader);

  void
  collectGarbage(const std::vector<BytecodeValue> &stack_values,
                 const std::vector<BytecodeValue> &locals,
                 const std::unordered_map<std::string, BytecodeValue> &globals,
                 const std::vector<uint32_t> &active_closure_ids,
                 const std::function<std::optional<BytecodeValue>(uint32_t)>
                     &open_local_reader);

private:
  void markValue(const BytecodeValue &value,
                 std::unordered_set<uint32_t> &marked_arrays,
                 std::unordered_set<uint32_t> &marked_objects,
                 std::unordered_set<uint32_t> &marked_sets,
                 std::unordered_set<uint32_t> &marked_closures,
                 const std::function<std::optional<BytecodeValue>(uint32_t)>
                     &open_local_reader) const;

  std::unordered_map<uint32_t, RuntimeClosure> closures_;
  std::unordered_map<uint32_t, std::vector<BytecodeValue>> arrays_;
  std::unordered_map<uint32_t, std::unordered_map<std::string, BytecodeValue>>
      objects_;
  std::unordered_map<uint32_t, std::unordered_map<std::string, BytecodeValue>>
      sets_;
  std::unordered_map<uint32_t, Range> ranges_;
  std::unordered_map<uint32_t, ErrorObject> errors_;
  std::unordered_map<uint32_t, std::vector<BytecodeValue>>
      structs_; // Field arrays (value type)
  std::unordered_map<uint32_t, std::vector<BytecodeValue>>
      classes_; // Field arrays (reference type)
  std::unordered_map<uint32_t, std::pair<uint32_t, std::vector<BytecodeValue>>>
      enums_; // tag + payload
  std::unordered_map<uint32_t, Iterator> iterators_;

  // Type registries
  std::vector<StructType> structTypes_;
  std::vector<ClassType> classTypes_;
  std::vector<EnumType> enumTypes_;

  uint32_t next_closure_id_ = 1;
  uint32_t next_array_id_ = 1;
  uint32_t next_object_id_ = 1;
  uint32_t next_set_id_ = 1;
  uint32_t next_range_id_ = 1;
  uint32_t next_error_id_ = 1;
  uint32_t next_iterator_id_ = 1;
  size_t allocation_budget_ = 1024;
  size_t allocations_since_last_ = 0;
  std::unordered_map<uint64_t, BytecodeValue> external_roots_;
  uint64_t next_external_root_id_ = 1;
  uint64_t collections_ = 0;
  uint64_t last_pause_ns_ = 0;
};

} // namespace havel::compiler
