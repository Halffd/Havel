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
  struct UpvalueCell {
    bool is_open = false;
    uint32_t open_index = 0;
    BytecodeValue closed_value = nullptr;
  };

  struct RuntimeClosure {
    uint32_t function_index = 0;
    std::vector<std::shared_ptr<UpvalueCell>> upvalues;
  };

  void reset();

  ClosureRef allocateClosure(RuntimeClosure closure);
  ArrayRef allocateArray();
  ObjectRef allocateObject();
  SetRef allocateSet();

  RuntimeClosure *closure(uint32_t id);
  const RuntimeClosure *closure(uint32_t id) const;
  std::vector<BytecodeValue> *array(uint32_t id);
  std::unordered_map<std::string, BytecodeValue> *object(uint32_t id);
  std::unordered_map<std::string, BytecodeValue> *set(uint32_t id);

  void setAllocationBudget(size_t value) { allocation_budget_ = value; }

  uint64_t pinExternalRoot(const BytecodeValue &value);
  bool unpinExternalRoot(uint64_t root_id);
  std::optional<BytecodeValue> externalRoot(uint64_t root_id) const;
  size_t externalRootCount() const { return external_roots_.size(); }

  void maybeCollectGarbage(
      const std::vector<BytecodeValue> &stack_values,
      const std::vector<BytecodeValue> &locals,
      const std::unordered_map<std::string, BytecodeValue> &globals,
      const std::vector<uint32_t> &active_closure_ids,
      const std::function<std::optional<BytecodeValue>(uint32_t)> &
          open_local_reader);

  void collectGarbage(
      const std::vector<BytecodeValue> &stack_values,
      const std::vector<BytecodeValue> &locals,
      const std::unordered_map<std::string, BytecodeValue> &globals,
      const std::vector<uint32_t> &active_closure_ids,
      const std::function<std::optional<BytecodeValue>(uint32_t)> &
          open_local_reader);

private:
  void markValue(const BytecodeValue &value, std::unordered_set<uint32_t> &marked_arrays,
                 std::unordered_set<uint32_t> &marked_objects,
                 std::unordered_set<uint32_t> &marked_sets,
                 std::unordered_set<uint32_t> &marked_closures,
                 const std::function<std::optional<BytecodeValue>(uint32_t)> &
                     open_local_reader) const;

  std::unordered_map<uint32_t, RuntimeClosure> closures_;
  std::unordered_map<uint32_t, std::vector<BytecodeValue>> arrays_;
  std::unordered_map<uint32_t, std::unordered_map<std::string, BytecodeValue>>
      objects_;
  std::unordered_map<uint32_t, std::unordered_map<std::string, BytecodeValue>>
      sets_;
  uint32_t next_closure_id_ = 1;
  uint32_t next_array_id_ = 1;
  uint32_t next_object_id_ = 1;
  uint32_t next_set_id_ = 1;
  size_t allocation_budget_ = 1024;
  size_t allocations_since_last_ = 0;
  std::unordered_map<uint64_t, BytecodeValue> external_roots_;
  uint64_t next_external_root_id_ = 1;
};

} // namespace havel::compiler
