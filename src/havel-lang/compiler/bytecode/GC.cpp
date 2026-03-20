#include "GC.hpp"

namespace havel::compiler {

void GCHeap::reset() {
  closures_.clear();
  arrays_.clear();
  objects_.clear();
  sets_.clear();
  next_closure_id_ = 1;
  next_array_id_ = 1;
  next_object_id_ = 1;
  next_set_id_ = 1;
  allocations_since_last_ = 0;
  external_roots_.clear();
  next_external_root_id_ = 1;
}

ClosureRef GCHeap::allocateClosure(RuntimeClosure closure) {
  const uint32_t id = next_closure_id_++;
  closures_.emplace(id, std::move(closure));
  return ClosureRef{.id = id};
}

ArrayRef GCHeap::allocateArray() {
  const uint32_t id = next_array_id_++;
  arrays_[id] = {};
  return ArrayRef{.id = id};
}

ObjectRef GCHeap::allocateObject() {
  const uint32_t id = next_object_id_++;
  objects_[id] = {};
  return ObjectRef{.id = id};
}

SetRef GCHeap::allocateSet() {
  const uint32_t id = next_set_id_++;
  sets_[id] = {};
  return SetRef{.id = id};
}

GCHeap::RuntimeClosure *GCHeap::closure(uint32_t id) {
  auto it = closures_.find(id);
  return it == closures_.end() ? nullptr : &it->second;
}

const GCHeap::RuntimeClosure *GCHeap::closure(uint32_t id) const {
  auto it = closures_.find(id);
  return it == closures_.end() ? nullptr : &it->second;
}

std::vector<BytecodeValue> *GCHeap::array(uint32_t id) {
  auto it = arrays_.find(id);
  return it == arrays_.end() ? nullptr : &it->second;
}

std::unordered_map<std::string, BytecodeValue> *GCHeap::object(uint32_t id) {
  auto it = objects_.find(id);
  return it == objects_.end() ? nullptr : &it->second;
}

std::unordered_map<std::string, BytecodeValue> *GCHeap::set(uint32_t id) {
  auto it = sets_.find(id);
  return it == sets_.end() ? nullptr : &it->second;
}

uint64_t GCHeap::pinExternalRoot(const BytecodeValue &value) {
  const uint64_t id = next_external_root_id_++;
  external_roots_[id] = value;
  return id;
}

bool GCHeap::unpinExternalRoot(uint64_t root_id) {
  return external_roots_.erase(root_id) > 0;
}

std::optional<BytecodeValue> GCHeap::externalRoot(uint64_t root_id) const {
  auto it = external_roots_.find(root_id);
  if (it == external_roots_.end()) {
    return std::nullopt;
  }
  return it->second;
}

void GCHeap::maybeCollectGarbage(
    const std::vector<BytecodeValue> &stack_values,
    const std::vector<BytecodeValue> &locals,
    const std::unordered_map<std::string, BytecodeValue> &globals,
    const std::vector<uint32_t> &active_closure_ids,
    const std::function<std::optional<BytecodeValue>(uint32_t)> &
        open_local_reader) {
  allocations_since_last_++;
  if (allocations_since_last_ < allocation_budget_) {
    return;
  }
  collectGarbage(stack_values, locals, globals, active_closure_ids,
                 open_local_reader);
}

void GCHeap::markValue(
    const BytecodeValue &value, std::unordered_set<uint32_t> &marked_arrays,
    std::unordered_set<uint32_t> &marked_objects,
    std::unordered_set<uint32_t> &marked_sets,
    std::unordered_set<uint32_t> &marked_closures,
    const std::function<std::optional<BytecodeValue>(uint32_t)> &
        open_local_reader) const {
  if (std::holds_alternative<ArrayRef>(value)) {
    uint32_t id = std::get<ArrayRef>(value).id;
    if (!marked_arrays.insert(id).second) {
      return;
    }
    auto it = arrays_.find(id);
    if (it == arrays_.end()) {
      return;
    }
    for (const auto &entry : it->second) {
      markValue(entry, marked_arrays, marked_objects, marked_sets,
                marked_closures, open_local_reader);
    }
    return;
  }

  if (std::holds_alternative<ObjectRef>(value)) {
    uint32_t id = std::get<ObjectRef>(value).id;
    if (!marked_objects.insert(id).second) {
      return;
    }
    auto it = objects_.find(id);
    if (it == objects_.end()) {
      return;
    }
    for (const auto &[_, entry] : it->second) {
      markValue(entry, marked_arrays, marked_objects, marked_sets,
                marked_closures, open_local_reader);
    }
    return;
  }

  if (std::holds_alternative<SetRef>(value)) {
    uint32_t id = std::get<SetRef>(value).id;
    if (!marked_sets.insert(id).second) {
      return;
    }
    auto it = sets_.find(id);
    if (it == sets_.end()) {
      return;
    }
    for (const auto &[_, entry] : it->second) {
      markValue(entry, marked_arrays, marked_objects, marked_sets,
                marked_closures, open_local_reader);
    }
    return;
  }

  if (std::holds_alternative<ClosureRef>(value)) {
    uint32_t id = std::get<ClosureRef>(value).id;
    if (!marked_closures.insert(id).second) {
      return;
    }
    auto it = closures_.find(id);
    if (it == closures_.end()) {
      return;
    }
    for (const auto &cell : it->second.upvalues) {
      if (!cell) {
        continue;
      }
      if (cell->is_open) {
        auto local_value = open_local_reader(cell->open_index);
        if (local_value.has_value()) {
          markValue(*local_value, marked_arrays, marked_objects, marked_sets,
                    marked_closures, open_local_reader);
        }
      } else {
        markValue(cell->closed_value, marked_arrays, marked_objects, marked_sets,
                  marked_closures, open_local_reader);
      }
    }
    return;
  }
}

void GCHeap::collectGarbage(
    const std::vector<BytecodeValue> &stack_values,
    const std::vector<BytecodeValue> &locals,
    const std::unordered_map<std::string, BytecodeValue> &globals,
    const std::vector<uint32_t> &active_closure_ids,
    const std::function<std::optional<BytecodeValue>(uint32_t)> &
        open_local_reader) {
  std::unordered_set<uint32_t> marked_arrays;
  std::unordered_set<uint32_t> marked_objects;
  std::unordered_set<uint32_t> marked_sets;
  std::unordered_set<uint32_t> marked_closures;

  for (const auto &value : stack_values) {
    markValue(value, marked_arrays, marked_objects, marked_sets, marked_closures,
              open_local_reader);
  }
  for (const auto &value : locals) {
    markValue(value, marked_arrays, marked_objects, marked_sets, marked_closures,
              open_local_reader);
  }
  for (const auto &[_, value] : globals) {
    markValue(value, marked_arrays, marked_objects, marked_sets, marked_closures,
              open_local_reader);
  }
  for (const auto &[_, value] : external_roots_) {
    markValue(value, marked_arrays, marked_objects, marked_sets, marked_closures,
              open_local_reader);
  }
  for (uint32_t closure_id : active_closure_ids) {
    if (closure_id == 0) {
      continue;
    }
    markValue(ClosureRef{.id = closure_id}, marked_arrays, marked_objects,
              marked_sets, marked_closures, open_local_reader);
  }

  for (auto it = arrays_.begin(); it != arrays_.end();) {
    if (marked_arrays.find(it->first) == marked_arrays.end()) {
      it = arrays_.erase(it);
    } else {
      ++it;
    }
  }
  for (auto it = objects_.begin(); it != objects_.end();) {
    if (marked_objects.find(it->first) == marked_objects.end()) {
      it = objects_.erase(it);
    } else {
      ++it;
    }
  }
  for (auto it = sets_.begin(); it != sets_.end();) {
    if (marked_sets.find(it->first) == marked_sets.end()) {
      it = sets_.erase(it);
    } else {
      ++it;
    }
  }
  for (auto it = closures_.begin(); it != closures_.end();) {
    if (marked_closures.find(it->first) == marked_closures.end()) {
      it = closures_.erase(it);
    } else {
      ++it;
    }
  }

  allocations_since_last_ = 0;
}

} // namespace havel::compiler
