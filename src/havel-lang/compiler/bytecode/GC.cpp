#include "GC.hpp"

#include <chrono>

namespace havel::compiler {

void GCHeap::reset() {
  closures_.clear();
  arrays_.clear();
  objects_.clear();
  sets_.clear();
  ranges_.clear();
  iterators_.clear();
  next_closure_id_ = 1;
  next_array_id_ = 1;
  next_object_id_ = 1;
  next_set_id_ = 1;
  next_range_id_ = 1;
  next_iterator_id_ = 1;
  allocations_since_last_ = 0;
  external_roots_.clear();
  next_external_root_id_ = 1;
  collections_ = 0;
  last_pause_ns_ = 0;
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

RangeRef GCHeap::allocateRange(int64_t start, int64_t end, int64_t step) {
  const uint32_t id = next_range_id_++;
  Range range;
  range.start = start;
  range.end = end;
  range.step = step;
  ranges_[id] = range;
  return RangeRef{.id = id};
}

GCHeap::Range *GCHeap::range(uint32_t id) {
  auto it = ranges_.find(id);
  return it == ranges_.end() ? nullptr : &it->second;
}

const GCHeap::Range *GCHeap::range(uint32_t id) const {
  auto it = ranges_.find(id);
  return it == ranges_.end() ? nullptr : &it->second;
}

IteratorRef GCHeap::allocateIterator(const BytecodeValue &iterable) {
  const uint32_t id = next_iterator_id_++;
  Iterator iter;
  iter.iterable = iterable;
  iter.index = 0;
  
  // For objects, pre-compute keys
  if (std::holds_alternative<ObjectRef>(iterable)) {
    auto *obj = object(std::get<ObjectRef>(iterable).id);
    if (obj) {
      for (const auto &[key, _] : *obj) {
        iter.keys.push_back(key);
      }
    }
  }
  
  iterators_[id] = std::move(iter);
  return IteratorRef{.id = id};
}

GCHeap::Iterator *GCHeap::iterator(uint32_t id) {
  auto it = iterators_.find(id);
  return it == iterators_.end() ? nullptr : &it->second;
}

const GCHeap::Iterator *GCHeap::iterator(uint32_t id) const {
  auto it = iterators_.find(id);
  return it == iterators_.end() ? nullptr : &it->second;
}

uint32_t GCHeap::createIterator(const BytecodeValue &iterable) {
  IteratorRef ref = allocateIterator(iterable);
  return ref.id;
}

BytecodeValue GCHeap::iteratorNext(uint32_t id) {
  auto *iter = iterator(id);
  if (!iter) {
    // Return {value: null, done: true}
    auto resultObj = allocateObject();
    auto *obj = object(resultObj.id);
    (*obj)["value"] = BytecodeValue(nullptr);
    (*obj)["done"] = BytecodeValue(true);
    return BytecodeValue(resultObj);
  }
  
  bool done = false;
  BytecodeValue value;
  
  // Dispatch based on iterable type
  if (std::holds_alternative<ArrayRef>(iter->iterable)) {
    auto *arr = array(std::get<ArrayRef>(iter->iterable).id);
    if (!arr || iter->index >= arr->size()) {
      done = true;
      value = nullptr;
    } else {
      value = (*arr)[iter->index++];
    }
  } else if (std::holds_alternative<std::string>(iter->iterable)) {
    const auto &str = std::get<std::string>(iter->iterable);
    if (iter->index >= str.length()) {
      done = true;
      value = std::string("");
    } else {
      value = std::string(1, str[iter->index++]);
    }
  } else if (std::holds_alternative<ObjectRef>(iter->iterable)) {
    if (iter->index >= iter->keys.size()) {
      done = true;
      value = nullptr;
    } else {
      // For objects, return just the key (like Python's for key in dict)
      // Multi-variable iteration will extract key/value from the iterator result
      value = iter->keys[iter->index++];
    }
  } else if (std::holds_alternative<RangeRef>(iter->iterable)) {
    auto *r = range(std::get<RangeRef>(iter->iterable).id);
    if (!r) {
      done = true;
      value = nullptr;
    } else {
      int64_t current = r->start + (iter->index * r->step);
      if ((r->step > 0 && current >= r->end) || (r->step < 0 && current <= r->end)) {
        done = true;
        value = nullptr;
      } else {
        value = BytecodeValue(current);
        iter->index++;
      }
    }
  } else {
    // Unknown type, just return done
    done = true;
    value = nullptr;
  }
  
  // Return {value, done}
  auto resultObj = allocateObject();
  auto *obj = object(resultObj.id);
  (*obj)["value"] = value;
  (*obj)["done"] = BytecodeValue(done);
  return BytecodeValue(resultObj);
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

GCHeap::Stats GCHeap::stats() const {
  uint64_t heap_size = 0;
  for (const auto &[_, array] : arrays_) {
    heap_size += sizeof(array) +
                 static_cast<uint64_t>(array.size()) * sizeof(BytecodeValue);
  }
  for (const auto &[_, object] : objects_) {
    heap_size += sizeof(object);
    for (const auto &[key, _] : object) {
      heap_size += static_cast<uint64_t>(key.size()) + sizeof(BytecodeValue);
    }
  }
  for (const auto &[_, set] : sets_) {
    heap_size += sizeof(set);
    for (const auto &[key, _] : set) {
      heap_size += static_cast<uint64_t>(key.size()) + sizeof(BytecodeValue);
    }
  }
  for (const auto &[_, closure] : closures_) {
    heap_size += sizeof(closure);
    heap_size += static_cast<uint64_t>(closure.upvalues.size()) *
                 sizeof(std::shared_ptr<UpvalueCell>);
  }

  return Stats{
      .heap_size = heap_size,
      .object_count = static_cast<uint64_t>(arrays_.size() + objects_.size() +
                                            sets_.size() + closures_.size()),
      .collections = collections_,
      .last_pause_ns = last_pause_ns_,
  };
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
  const auto pause_start = std::chrono::steady_clock::now();

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
  collections_++;
  last_pause_ns_ = static_cast<uint64_t>(
      std::chrono::duration_cast<std::chrono::nanoseconds>(
          std::chrono::steady_clock::now() - pause_start)
          .count());
}

} // namespace havel::compiler
