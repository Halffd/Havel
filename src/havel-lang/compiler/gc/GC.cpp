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
  strings_.clear();
  enums_.clear();
  enumTypes_.clear();
  next_closure_id_ = 1;
  next_array_id_ = 1;
  next_object_id_ = 1;
  next_set_id_ = 1;
  next_range_id_ = 1;
  next_iterator_id_ = 1;
  next_string_id_ = 1;
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

StringRef GCHeap::allocateString(std::string value) {
  const uint32_t id = next_string_id_++;
  strings_.emplace(id, std::move(value));
  return StringRef{.id = id};
}

std::string *GCHeap::string(uint32_t id) {
  auto it = strings_.find(id);
  return it == strings_.end() ? nullptr : &it->second;
}

const std::string *GCHeap::string(uint32_t id) const {
  auto it = strings_.find(id);
  return it == strings_.end() ? nullptr : &it->second;
}

ArrayRef GCHeap::allocateArray() {
  const uint32_t id = next_array_id_++;
  arrays_[id] = {};
  return ArrayRef{.id = id};
}

ObjectRef GCHeap::allocateObject(bool sorted) {
  const uint32_t id = next_object_id_++;
  ObjectEntry entry;
  entry.sorted = sorted;
  objects_[id] = std::move(entry);
  return ObjectRef{.id = id, .sorted = sorted};
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

ErrorRef GCHeap::allocateError(const std::string &errorType,
                               const std::string &message,
                               const std::string &stackTrace, uint32_t line,
                               uint32_t column) {
  const uint32_t id = next_error_id_++;
  errors_[id] = ErrorObject(errorType, message, stackTrace, line, column);
  return ErrorRef{.id = id};
}

GCHeap::Range *GCHeap::range(uint32_t id) {
  auto it = ranges_.find(id);
  return it == ranges_.end() ? nullptr : &it->second;
}

const GCHeap::Range *GCHeap::range(uint32_t id) const {
  auto it = ranges_.find(id);
  return it == ranges_.end() ? nullptr : &it->second;
}

GCHeap::ErrorObject *GCHeap::error(uint32_t id) {
  auto it = errors_.find(id);
  return it == errors_.end() ? nullptr : &it->second;
}

const GCHeap::ErrorObject *GCHeap::error(uint32_t id) const {
  auto it = errors_.find(id);
  return it == errors_.end() ? nullptr : &it->second;
}

IteratorRef GCHeap::allocateIterator(const Value &iterable) {
  const uint32_t id = next_iterator_id_++;
  Iterator iter;
  iter.iterable = iterable;
  iter.index = 0;

  // For objects, pre-compute keys
  if (iterable.isObjectId()) {
    auto *obj = object(iterable.asObjectId());
    if (obj) {
      iter.keys = obj->getKeys();
    }
  } else if (iterable.isSetId()) {
    auto *setObj = set(iterable.asSetId());
    if (setObj) {
      for (const auto &[key, present] : *setObj) {
        if (present.isBool() && !present.asBool()) {
          continue;
        }
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

uint32_t GCHeap::createIterator(const Value &iterable) {
  IteratorRef ref = allocateIterator(iterable);
  return ref.id;
}


uint32_t GCHeap::registerEnumType(const std::string &name,
                                  const std::vector<std::string> &variants) {
  uint32_t id = static_cast<uint32_t>(enumTypes_.size());
  enumTypes_.push_back(EnumType{name, variants});
  return id;
}

// Enum allocation
EnumRef GCHeap::allocateEnum(uint32_t typeId, uint32_t tag,
                             size_t payloadCount) {
  const uint32_t id = next_array_id_++;
  enums_[id] = {
      tag, std::vector<Value>(payloadCount, Value::makeNull())};
  return EnumRef{.id = id, .tag = tag, .typeId = typeId};
}

Value GCHeap::iteratorNext(uint32_t id) {
  auto *iter = iterator(id);
  if (!iter) {
    // Return {value: null, done: true}
    auto resultObj = allocateObject();
    auto *obj = object(resultObj.id);
    (*obj)["value"] = Value::makeNull();
    (*obj)["done"] = Value::makeBool(true);
    return Value::makeObjectId(resultObj.id);
  }

  bool done = false;
  Value value;

  // Dispatch based on iterable type
  if (iter->iterable.isArrayId()) {
    auto *arr = array(iter->iterable.asArrayId());
    if (!arr || iter->index >= arr->size()) {
      done = true;
      value = Value::makeNull();
    } else {
      value = (*arr)[iter->index++];
    }
  } else if (iter->iterable.isStringValId()) {
    // StringValId iteration - fall back to numeric range (legacy)
    // New code should use STRING_PROMOTE to convert to StringId first
    if (iter->index >= 256) {
      done = true;
      value = Value::makeNull();
    } else {
      value = Value::makeInt(iter->index++);
    }
  } else if (iter->iterable.isStringId()) {
    // StringId iteration - return each character as a new StringId
    auto *s = string(iter->iterable.asStringId());
    if (!s || iter->index >= s->size()) {
      done = true;
      value = Value::makeNull();
    } else {
      // Allocate a new StringId for the single character
      char c = (*s)[iter->index++];
      auto charStrRef = allocateString(std::string(1, c));
      value = Value::makeStringId(charStrRef.id);
    }
  } else if (iter->iterable.isObjectId()) {
    if (iter->index >= iter->keys.size()) {
      done = true;
      value = Value::makeNull();
    } else {
      // For objects, return {key, value} object
      auto key = iter->keys[iter->index];
      auto *obj = object(iter->iterable.asObjectId());
      Value val = Value::makeNull();
      if (obj) {
        auto it = obj->find(key);
        if (it != obj->end()) {
          val = it->second;
        }
      }
      auto resultObj = allocateObject();
      auto *result = object(resultObj.id);
      // Allocate a runtime StringId for the key
      auto keyStrRef = allocateString(key);
      (*result)["key"] = Value::makeStringId(keyStrRef.id);
      (*result)["value"] = val;
      value = Value::makeObjectId(resultObj.id);
      iter->index++;
    }
  } else if (iter->iterable.isSetId()) {
    if (iter->index >= iter->keys.size()) {
      done = true;
      value = Value::makeNull();
    } else {
      // Return set element as string ID
      value = Value::makeNull(); // Placeholder
      iter->index++;
    }
  } else if (iter->iterable.isRangeId()) {
    auto *r = range(iter->iterable.asRangeId());
    if (!r) {
      done = true;
      value = Value::makeNull();
    } else {
      int64_t current = r->start + (iter->index * r->step);
      if ((r->step > 0 && current >= r->end) ||
          (r->step < 0 && current <= r->end)) {
        done = true;
        value = Value::makeNull();
      } else {
        value = Value::makeInt(current);
        iter->index++;
      }
    }
  } else {
    // Unknown type, just return done
    done = true;
    value = Value::makeNull();
  }

  // Return {value, done}
  auto resultObj = allocateObject();
  auto *obj = object(resultObj.id);
  (*obj)["value"] = value;
  (*obj)["done"] = Value::makeBool(done);
  return Value::makeObjectId(resultObj.id);
}

GCHeap::RuntimeClosure *GCHeap::closure(uint32_t id) {
  auto it = closures_.find(id);
  return it == closures_.end() ? nullptr : &it->second;
}

const GCHeap::RuntimeClosure *GCHeap::closure(uint32_t id) const {
  auto it = closures_.find(id);
  return it == closures_.end() ? nullptr : &it->second;
}

std::vector<Value> *GCHeap::array(uint32_t id) {
  auto it = arrays_.find(id);
  return it == arrays_.end() ? nullptr : &it->second;
}

const std::vector<Value> *GCHeap::array(uint32_t id) const {
  auto it = arrays_.find(id);
  return it == arrays_.end() ? nullptr : &it->second;
}

GCHeap::ObjectEntry *GCHeap::object(uint32_t id) {
  auto it = objects_.find(id);
  return it == objects_.end() ? nullptr : &it->second;
}

const GCHeap::ObjectEntry *GCHeap::object(uint32_t id) const {
  auto it = objects_.find(id);
  return it == objects_.end() ? nullptr : &it->second;
}

std::unordered_map<std::string, Value> *GCHeap::set(uint32_t id) {
  auto it = sets_.find(id);
  return it == sets_.end() ? nullptr : &it->second;
}

const std::unordered_map<std::string, Value> *GCHeap::set(uint32_t id) const {
  auto it = sets_.find(id);
  return it == sets_.end() ? nullptr : &it->second;
}

uint64_t GCHeap::pinExternalRoot(const Value &value) {
  const uint64_t id = next_external_root_id_++;
  external_roots_[id] = value;
  return id;
}

bool GCHeap::unpinExternalRoot(uint64_t root_id) {
  return external_roots_.erase(root_id) > 0;
}

std::optional<Value> GCHeap::externalRoot(uint64_t root_id) const {
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
                 static_cast<uint64_t>(array.size()) * sizeof(Value);
  }
  for (const auto &[_, object] : objects_) {
    heap_size += sizeof(object);
    for (const auto &[key, _] : object.data) {
      heap_size += static_cast<uint64_t>(key.size()) + sizeof(Value);
    }
  }
  for (const auto &[_, set] : sets_) {
    heap_size += sizeof(set);
    for (const auto &[key, _] : set) {
      heap_size += static_cast<uint64_t>(key.size()) + sizeof(Value);
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
    const std::vector<Value> &stack_values,
    const std::vector<Value> &locals,
    const std::unordered_map<std::string, Value> &globals,
    const std::vector<uint32_t> &active_closure_ids,
    const std::function<std::optional<Value>(uint32_t)>
        &open_local_reader) {
  allocations_since_last_++;
  if (allocations_since_last_ < allocation_budget_) {
    return;
  }
  collectGarbage(stack_values, locals, globals, active_closure_ids,
                 open_local_reader);
}

void GCHeap::markValue(
    const Value &value, std::unordered_set<uint32_t> &marked_arrays,
    std::unordered_set<uint32_t> &marked_objects,
    std::unordered_set<uint32_t> &marked_sets,
    std::unordered_set<uint32_t> &marked_closures,
    const std::function<std::optional<Value>(uint32_t)>
        &open_local_reader) const {
  if (value.isArrayId()) {
    uint32_t id = value.asArrayId();
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

  if (value.isObjectId()) {
    uint32_t id = value.asObjectId();
    if (!marked_objects.insert(id).second) {
      return;
    }
    auto it = objects_.find(id);
    if (it == objects_.end()) {
      return;
    }
    for (const auto &[_, entry] : it->second.data) {
      markValue(entry, marked_arrays, marked_objects, marked_sets,
                marked_closures, open_local_reader);
    }
    return;
  }

  if (value.isSetId()) {
    uint32_t id = value.asSetId();
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



  if (value.isEnumId()) {
    // Enums have a payload array that needs marking
    uint32_t id = value.asEnumId();
    auto it = enums_.find(id);
    if (it == enums_.end()) {
      return;
    }
    for (const auto &entry : it->second.second) {
      markValue(entry, marked_arrays, marked_objects, marked_sets,
                marked_closures, open_local_reader);
    }
    return;
  }

  if (value.isClosureId()) {
    uint32_t id = ClosureRef{value.asClosureId()}.id;
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
        markValue(cell->closed_value, marked_arrays, marked_objects,
                  marked_sets, marked_closures, open_local_reader);
      }
    }
    return;
  }
}

void GCHeap::collectGarbage(
    const std::vector<Value> &stack_values,
    const std::vector<Value> &locals,
    const std::unordered_map<std::string, Value> &globals,
    const std::vector<uint32_t> &active_closure_ids,
    const std::function<std::optional<Value>(uint32_t)>
        &open_local_reader) {
  const auto pause_start = std::chrono::steady_clock::now();

  std::unordered_set<uint32_t> marked_arrays;
  std::unordered_set<uint32_t> marked_objects;
  std::unordered_set<uint32_t> marked_sets;
  std::unordered_set<uint32_t> marked_closures;

  for (const auto &value : stack_values) {
    markValue(value, marked_arrays, marked_objects, marked_sets,
              marked_closures, open_local_reader);
  }
  for (const auto &value : locals) {
    markValue(value, marked_arrays, marked_objects, marked_sets,
              marked_closures, open_local_reader);
  }
  for (const auto &[_, value] : globals) {
    markValue(value, marked_arrays, marked_objects, marked_sets,
              marked_closures, open_local_reader);
  }
  for (const auto &[_, value] : external_roots_) {
    markValue(value, marked_arrays, marked_objects, marked_sets,
              marked_closures, open_local_reader);
  }
  for (uint32_t closure_id : active_closure_ids) {
    if (closure_id == 0) {
      continue;
    }
    markValue(Value::makeClosureId(closure_id), marked_arrays, marked_objects,
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

std::shared_ptr<GCHeap::UpvalueCell> GCHeap::createUpvalue(uint32_t index) {
  auto cell = std::make_shared<UpvalueCell>();
  cell->is_open = true;
  cell->open_index = index;
  return cell;
}

// ============================================================================
// Concurrency Object Management
// ============================================================================

ThreadRef GCHeap::allocateThreadObj(std::shared_ptr<::havel::Thread> thread) {
  const uint32_t id = next_thread_id_++;
  threads_.emplace(id, std::move(thread));
  return ThreadRef{.id = id};
}

IntervalRef GCHeap::allocateIntervalObj(std::shared_ptr<::havel::Interval> interval) {
  const uint32_t id = next_interval_id_++;
  intervals_.emplace(id, std::move(interval));
  return IntervalRef{.id = id};
}

TimeoutRef GCHeap::allocateTimeoutObj(std::shared_ptr<::havel::Timeout> timeout) {
  const uint32_t id = next_timeout_id_++;
  timeouts_.emplace(id, std::move(timeout));
  return TimeoutRef{.id = id};
}

::havel::Thread* GCHeap::thread(uint32_t id) {
  auto it = threads_.find(id);
  return it == threads_.end() ? nullptr : it->second.get();
}

const ::havel::Thread* GCHeap::thread(uint32_t id) const {
  auto it = threads_.find(id);
  return it == threads_.end() ? nullptr : it->second.get();
}

::havel::Interval* GCHeap::interval(uint32_t id) {
  auto it = intervals_.find(id);
  return it == intervals_.end() ? nullptr : it->second.get();
}

const ::havel::Interval* GCHeap::interval(uint32_t id) const {
  auto it = intervals_.find(id);
  return it == intervals_.end() ? nullptr : it->second.get();
}

::havel::Timeout* GCHeap::timeout(uint32_t id) {
  auto it = timeouts_.find(id);
  return it == timeouts_.end() ? nullptr : it->second.get();
}

const ::havel::Timeout* GCHeap::timeout(uint32_t id) const {
  auto it = timeouts_.find(id);
  return it == timeouts_.end() ? nullptr : it->second.get();
}

} // namespace havel::compiler
