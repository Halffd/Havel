#include "GC.hpp"

#include <chrono>
#include <limits>
#include "utils/Logger.hpp"

namespace havel::compiler {

namespace {
constexpr size_t kMinAllocationBudget = 256;
constexpr size_t kMaxAllocationBudget = 1 << 20;
constexpr size_t kDefaultWorkBudget = 1024;
constexpr size_t kMaxIterationsPerStep = 100000;
}

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
    threads_.clear();
    intervals_.clear();
    timeouts_.clear();
    channels_.clear();
    coroutines_.clear();
    errors_.clear();

    next_closure_id_ = 1;
    next_array_id_ = 1;
    next_object_id_ = 1;
    next_set_id_ = 1;
    next_range_id_ = 1;
    next_iterator_id_ = 1;
    next_string_id_ = 1;
    next_thread_id_ = 1;
    next_interval_id_ = 1;
    next_timeout_id_ = 1;
    next_channel_id_ = 1;
    next_coroutine_id_ = 1;
    next_error_id_ = 1;

    allocations_since_last_ = 0;
    recovered_in_cycle_ = 0;
    external_roots_.clear();
    next_external_root_id_ = 1;
    collections_ = 0;
    last_pause_ns_ = 0;
    total_recovered_ = 0;
    gc_state_ = IncrementalState::Idle;
    collection_requested_ = false;

    mark_worklist_.clear();
    marked_arrays_.clear();
    marked_objects_.clear();
    marked_sets_.clear();
    marked_closures_.clear();
    marked_strings_.clear();
    marked_iterators_.clear();
    marked_ranges_.clear();
    marked_errors_.clear();
    marked_enums_.clear();
    marked_coroutines_.clear();
    marked_threads_.clear();
    marked_intervals_.clear();
    marked_timeouts_.clear();
    marked_channels_.clear();

    array_ages_.clear();
    object_ages_.clear();
    set_ages_.clear();
    closure_ages_.clear();
    old_arrays_.clear();
    old_objects_.clear();
    old_sets_.clear();
    old_closures_.clear();

    current_collection_full_ = false;
    minor_collections_since_full_ = 0;

    root_stack_snapshot_.clear();
    root_locals_snapshot_.clear();
    root_globals_snapshot_.clear();
    root_closures_snapshot_.clear();
    open_local_reader_snapshot_ = {};

    sweep_keys_.clear();
    sweep_index_ = 0;
    sweep_phase_ = IncrementalState::Idle;
}

ClosureRef GCHeap::allocateClosure(RuntimeClosure closure) {
    const uint32_t id = next_closure_id_++;
    closures_.emplace(id, std::move(closure));
    closure_ages_[id] = 0;
    old_closures_.erase(id);
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
    array_ages_[id] = 0;
    old_arrays_.erase(id);
    return ArrayRef{.id = id};
}

ObjectRef GCHeap::allocateObject(bool sorted) {
    const uint32_t id = next_object_id_++;
    ObjectEntry entry;
    entry.sorted = sorted;
    objects_[id] = std::move(entry);
    object_ages_[id] = 0;
    old_objects_.erase(id);
    return ObjectRef{.id = id, .sorted = sorted};
}

SetRef GCHeap::allocateSet() {
    const uint32_t id = next_set_id_++;
    sets_[id] = {};
    set_ages_[id] = 0;
    old_sets_.erase(id);
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
    const std::string &stackTrace, uint32_t line, uint32_t column) {
    const uint32_t id = next_error_id_++;
    errors_[id] = ErrorObject(errorType, message, stackTrace, line, column);
    return ErrorRef{.id = id};
}

IteratorRef GCHeap::allocateIterator(const Value &iterable) {
    const uint32_t id = next_iterator_id_++;
    Iterator iter;
    iter.iterable = iterable;
    iter.index = 0;

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

EnumRef GCHeap::allocateEnum(uint32_t typeId, uint32_t tag, size_t payloadCount) {
    const uint32_t id = next_array_id_++;
    enums_[id] = {tag, std::vector<Value>(payloadCount, Value::makeNull())};
    return EnumRef{.id = id, .tag = tag, .typeId = typeId};
}

Value GCHeap::iteratorNext(uint32_t id) {
    auto *iter = iterator(id);
    if (!iter) {
        auto resultObj = allocateObject();
        auto *obj = object(resultObj.id);
        (*obj)["first"] = Value::makeNull();
        (*obj)["second"] = Value::makeNull();
        (*obj)["done"] = Value::makeBool(true);
        return Value::makeObjectId(resultObj.id);
    }

    bool done = false;
    Value first;
    Value second;

    if (iter->iterable.isArrayId()) {
        auto *arr = array(iter->iterable.asArrayId());
        if (!arr || iter->index >= arr->size()) {
            done = true;
            first = Value::makeNull();
            second = Value::makeNull();
        } else {
            first = Value::makeInt(iter->index);
            second = (*arr)[iter->index++];
        }
    } else if (iter->iterable.isStringValId()) {
        if (iter->index >= 256) {
            done = true;
            first = Value::makeNull();
            second = Value::makeNull();
        } else {
            first = Value::makeInt(iter->index);
            second = Value::makeInt(iter->index++);
        }
    } else if (iter->iterable.isStringId()) {
        auto *s = string(iter->iterable.asStringId());
        if (!s || iter->index >= s->size()) {
            done = true;
            first = Value::makeNull();
            second = Value::makeNull();
        } else {
            first = Value::makeInt(iter->index);
            char c = (*s)[iter->index++];
            auto charStrRef = allocateString(std::string(1, c));
            second = Value::makeStringId(charStrRef.id);
        }
    } else if (iter->iterable.isObjectId()) {
        if (iter->index >= iter->keys.size()) {
            done = true;
            first = Value::makeNull();
            second = Value::makeNull();
        } else {
            auto key = iter->keys[iter->index];
            auto keyStrRef = allocateString(key);
            first = Value::makeStringId(keyStrRef.id);
            auto *obj = object(iter->iterable.asObjectId());
            if (obj) {
                auto *val = obj->get(key);
                second = val ? *val : Value::makeNull();
            } else {
                second = Value::makeNull();
            }
            iter->index++;
        }
    } else if (iter->iterable.isSetId()) {
        if (iter->index >= iter->keys.size()) {
            done = true;
            first = Value::makeNull();
            second = Value::makeNull();
        } else {
            auto key = iter->keys[iter->index++];
            try {
                first = Value::makeInt(std::stoll(key));
                second = first;
            } catch (...) {
                auto strRef = allocateString(key);
                first = Value::makeStringId(strRef.id);
                second = first;
            }
        }
    } else if (iter->iterable.isRangeId()) {
        auto *r = range(iter->iterable.asRangeId());
        if (!r) {
            done = true;
            first = Value::makeNull();
            second = Value::makeNull();
        } else {
            int64_t current = r->start + (iter->index * r->step);
            if ((r->step > 0 && current > r->end) || (r->step < 0 && current < r->end)) {
                done = true;
                first = Value::makeNull();
                second = Value::makeNull();
            } else {
                first = Value::makeInt(iter->index);
                second = Value::makeInt(current);
                iter->index++;
            }
        }
    } else {
        done = true;
        first = Value::makeNull();
        second = Value::makeNull();
    }

    auto resultObj = allocateObject();
    auto *obj = object(resultObj.id);
    (*obj)["first"] = first;
    (*obj)["second"] = second;
    (*obj)["done"] = Value::makeBool(done);
    return Value::makeObjectId(resultObj.id);
}

void GCHeap::setAllocationBudget(size_t value) {
    allocation_budget_ = std::clamp(value, kMinAllocationBudget, kMaxAllocationBudget);
}

bool GCHeap::isCollectionInProgress() const {
    return gc_state_ != IncrementalState::Idle;
}

GCHeap::RuntimeClosure *GCHeap::closure(uint32_t id) {
    auto it = closures_.find(id);
    return it == closures_.end() ? nullptr : &it->second;
}

const GCHeap::RuntimeClosure *GCHeap::closure(uint32_t id) const {
    auto it = closures_.find(id);
    return it == closures_.end() ? nullptr : &it->second;
}

GCHeap::ArrayEntry *GCHeap::array(uint32_t id) {
    auto it = arrays_.find(id);
    return it == arrays_.end() ? nullptr : &it->second;
}

const GCHeap::ArrayEntry *GCHeap::array(uint32_t id) const {
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

GCHeap::Range *GCHeap::range(uint32_t id) {
    auto it = ranges_.find(id);
    return it == ranges_.end() ? nullptr : &it->second;
}

const GCHeap::Range *GCHeap::range(uint32_t id) const {
    auto it = ranges_.find(id);
    return it == ranges_.end() ? nullptr : &it->second;
}

GCHeap::Iterator *GCHeap::iterator(uint32_t id) {
    auto it = iterators_.find(id);
    return it == iterators_.end() ? nullptr : &it->second;
}

const GCHeap::Iterator *GCHeap::iterator(uint32_t id) const {
    auto it = iterators_.find(id);
    return it == iterators_.end() ? nullptr : &it->second;
}

GCHeap::ErrorObject *GCHeap::error(uint32_t id) {
    auto it = errors_.find(id);
    return it == errors_.end() ? nullptr : &it->second;
}

const GCHeap::ErrorObject *GCHeap::error(uint32_t id) const {
    auto it = errors_.find(id);
    return it == errors_.end() ? nullptr : &it->second;
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
    for (const auto &[_, str] : strings_) {
        heap_size += str.size();
    }
    for (const auto &[_, iter] : iterators_) {
        heap_size += sizeof(iter) + iter.keys.size() * sizeof(std::string);
    }
    for (const auto &[_, err] : errors_) {
        heap_size += err.errorType.size() + err.message.size() + err.stackTrace.size();
    }
    for (const auto &[_, co] : coroutines_) {
        heap_size += co.stack.size() * sizeof(Value) + co.locals.size() * sizeof(Value);
    }
    for (const auto &[_, ch] : channels_) {
        heap_size += ch.size() * sizeof(Value);
    }

    return Stats{
        .heap_size = heap_size,
        .object_count = static_cast<uint64_t>(arrays_.size() + objects_.size() +
            sets_.size() + closures_.size() + strings_.size() + iterators_.size() +
            errors_.size() + coroutines_.size() + ranges_.size() + enums_.size()),
        .collections = collections_,
        .last_pause_ns = last_pause_ns_,
        .total_recovered = total_recovered_,
    };
}

void GCHeap::maybeCollectGarbage(
    const std::vector<Value> &stack_values,
    const std::vector<Value> &locals,
    const std::unordered_map<std::string, Value> &globals,
    const std::vector<uint32_t> &active_closure_ids,
    const std::function<std::optional<Value>(uint32_t)> &open_local_reader) {

    allocations_since_last_++;

    if (allocations_since_last_ >= allocation_budget_) {
        collection_requested_ = true;
    }

    if (collection_requested_ && gc_state_ == IncrementalState::Idle) {
        stepGarbageCollection(stack_values, locals, globals, active_closure_ids,
            open_local_reader, kDefaultWorkBudget);
    }
}

void GCHeap::collectGarbage(
    const std::vector<Value> &stack_values,
    const std::vector<Value> &locals,
    const std::unordered_map<std::string, Value> &globals,
    const std::vector<uint32_t> &active_closure_ids,
    const std::function<std::optional<Value>(uint32_t)> &open_local_reader) {

    startIncrementalCollection(stack_values, locals, globals, active_closure_ids, open_local_reader);

    size_t iterations = 0;
    while (isCollectionInProgress() && iterations < kMaxIterationsPerStep) {
        size_t budget = std::numeric_limits<size_t>::max();
        sweepStep(budget);
        iterations++;
    }

    if (isCollectionInProgress()) {
        ::havel::warning("[GC] Collection exceeded max iterations, forcing completion");
        completeCollection();
    }
}

void GCHeap::forceFullCollection(
    const std::vector<Value> &stack_values,
    const std::vector<Value> &locals,
    const std::unordered_map<std::string, Value> &globals,
    const std::vector<uint32_t> &active_closure_ids,
    const std::function<std::optional<Value>(uint32_t)> &open_local_reader) {

    current_collection_full_ = true;
    collectGarbage(stack_values, locals, globals, active_closure_ids, open_local_reader);
}

void GCHeap::stepGarbageCollection(
    const std::vector<Value> &stack_values,
    const std::vector<Value> &locals,
    const std::unordered_map<std::string, Value> &globals,
    const std::vector<uint32_t> &active_closure_ids,
    const std::function<std::optional<Value>(uint32_t)> &open_local_reader,
    size_t work_budget) {

    if (work_budget == 0) {
        return;
    }

    if (gc_state_ == IncrementalState::Idle) {
        if (!collection_requested_ && allocations_since_last_ < allocation_budget_) {
            return;
        }
        startIncrementalCollection(stack_values, locals, globals, active_closure_ids, open_local_reader);
    }

    const auto pause_start = std::chrono::steady_clock::now();

    size_t iterations = 0;
    while (work_budget > 0 && gc_state_ != IncrementalState::Idle &&
           iterations < kMaxIterationsPerStep) {
        if (gc_state_ == IncrementalState::Mark) {
            markStep(work_budget);
        } else {
            sweepStep(work_budget);
        }
        iterations++;
    }

    if (gc_state_ == IncrementalState::Idle) {
        completeCollection();
    }

    const auto elapsed_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(
        std::chrono::steady_clock::now() - pause_start);
    last_pause_ns_ = static_cast<uint64_t>(elapsed_ns.count());
}

void GCHeap::startIncrementalCollection(
    const std::vector<Value> &stack_values,
    const std::vector<Value> &locals,
    const std::unordered_map<std::string, Value> &globals,
    const std::vector<uint32_t> &active_closure_ids,
    const std::function<std::optional<Value>(uint32_t)> &open_local_reader) {

    root_stack_snapshot_ = stack_values;
    root_locals_snapshot_ = locals;
    root_globals_snapshot_ = globals;
    root_closures_snapshot_ = active_closure_ids;
    open_local_reader_snapshot_ = open_local_reader;

    mark_worklist_.clear();
    marked_arrays_.clear();
    marked_objects_.clear();
    marked_sets_.clear();
    marked_closures_.clear();
    marked_strings_.clear();
    marked_iterators_.clear();
    marked_ranges_.clear();
    marked_errors_.clear();
    marked_enums_.clear();
    marked_coroutines_.clear();
    marked_threads_.clear();
    marked_intervals_.clear();
    marked_timeouts_.clear();
    marked_channels_.clear();

    recovered_in_cycle_ = 0;
    collection_requested_ = false;

    if (minor_collections_since_full_ >= full_collection_interval_) {
        current_collection_full_ = true;
    }

    gc_state_ = IncrementalState::Mark;
    markRoots();

    ::havel::debug("[GC] Started {} collection (budget: {}, objects: {})",
        current_collection_full_ ? "full" : "minor",
        allocation_budget_,
        arrays_.size() + objects_.size() + sets_.size() + closures_.size());
}

void GCHeap::markReference(const Value &value) {
    if (value.isArrayId()) {
        const uint32_t id = value.asArrayId();
        if (marked_arrays_.insert(id).second) {
            mark_worklist_.push_back(value);
        }
        return;
    }
    if (value.isObjectId()) {
        const uint32_t id = value.asObjectId();
        if (marked_objects_.insert(id).second) {
            mark_worklist_.push_back(value);
        }
        return;
    }
    if (value.isSetId()) {
        const uint32_t id = value.asSetId();
        if (marked_sets_.insert(id).second) {
            mark_worklist_.push_back(value);
        }
        return;
    }
    if (value.isClosureId()) {
        const uint32_t id = value.asClosureId();
        if (marked_closures_.insert(id).second) {
            mark_worklist_.push_back(value);
        }
        return;
    }
    if (value.isStringId()) {
        marked_strings_.insert(value.asStringId());
        return;
    }
    if (value.isRangeId()) {
        marked_ranges_.insert(value.asRangeId());
        return;
    }
    if (value.isErrorId()) {
        marked_errors_.insert(value.asErrorId());
        if (auto *err = error(value.asErrorId()); err && !err->cause.isNull()) {
            markReference(err->cause);
        }
        return;
    }
    if (value.isIteratorId()) {
        marked_iterators_.insert(value.asIteratorId());
        return;
    }
    if (value.isEnumId()) {
        marked_enums_.insert(value.asEnumId());
        auto it = enums_.find(value.asEnumId());
        if (it != enums_.end()) {
            for (const auto &entry : it->second.second) {
                markReference(entry);
            }
        }
        return;
    }
    if (value.isThreadId()) {
        marked_threads_.insert(value.asThreadId());
        return;
    }
    if (value.isIntervalId()) {
        marked_intervals_.insert(value.asIntervalId());
        return;
    }
    if (value.isTimeoutId()) {
        marked_timeouts_.insert(value.asTimeoutId());
        return;
    }
    if (value.isChannelId()) {
        marked_channels_.insert(value.asChannelId());
        return;
    }
    if (value.isCoroutineId()) {
        marked_coroutines_.insert(value.asCoroutineId());
        return;
    }
}

void GCHeap::markRoots() {
    for (const auto &value : root_stack_snapshot_) {
        markReference(value);
    }
    for (const auto &value : root_locals_snapshot_) {
        markReference(value);
    }
    for (const auto &[_, value] : root_globals_snapshot_) {
        markReference(value);
    }
    for (const auto &[_, value] : external_roots_) {
        markReference(value);
    }
    for (uint32_t closure_id : root_closures_snapshot_) {
        if (closure_id != 0) {
            markReference(Value::makeClosureId(closure_id));
        }
    }

    if (!current_collection_full_) {
        for (uint32_t id : old_arrays_) {
            markReference(Value::makeArrayId(id));
        }
        for (uint32_t id : old_objects_) {
            markReference(Value::makeObjectId(id));
        }
        for (uint32_t id : old_sets_) {
            markReference(Value::makeSetId(id));
        }
        for (uint32_t id : old_closures_) {
            markReference(Value::makeClosureId(id));
        }
    }
}

void GCHeap::markStep(size_t &work_budget) {
    while (work_budget > 0 && !mark_worklist_.empty()) {
        Value current = mark_worklist_.back();
        mark_worklist_.pop_back();
        work_budget--;

        if (current.isArrayId()) {
            auto it = arrays_.find(current.asArrayId());
            if (it == arrays_.end()) {
                continue;
            }
            for (const auto &entry : it->second) {
                markReference(entry);
            }
            continue;
        }
        if (current.isObjectId()) {
            auto it = objects_.find(current.asObjectId());
            if (it == objects_.end()) {
                continue;
            }
            for (const auto &[_, entry] : it->second.data) {
                markReference(entry);
            }
            continue;
        }
        if (current.isSetId()) {
            auto it = sets_.find(current.asSetId());
            if (it == sets_.end()) {
                continue;
            }
            for (const auto &[_, entry] : it->second) {
                markReference(entry);
            }
            continue;
        }
        if (current.isClosureId()) {
            auto it = closures_.find(current.asClosureId());
            if (it == closures_.end()) {
                continue;
            }
            for (const auto &cell : it->second.upvalues) {
                if (!cell) {
                    continue;
                }
                if (cell->is_open) {
                    auto local_value = open_local_reader_snapshot_(cell->open_index);
                    if (local_value.has_value()) {
                        markReference(*local_value);
                    }
                } else {
                    markReference(cell->closed_value);
                }
            }
            continue;
        }
    }

    if (mark_worklist_.empty()) {
        gc_state_ = IncrementalState::SweepArrays;
        snapshotSweepKeys();
    }
}

void GCHeap::snapshotSweepKeys() {
    sweep_keys_.clear();
    sweep_index_ = 0;
    sweep_phase_ = gc_state_;
}

void GCHeap::sweepStep(size_t &work_budget) {
    auto sweep_container = [this, &work_budget](auto &container, auto &marked_set,
        auto &ages_map, auto &old_set, auto promote_fn) -> bool {
        if (sweep_index_ >= sweep_keys_.size()) {
            return true;
        }

        uint32_t id = sweep_keys_[sweep_index_++];
        work_budget--;

        auto it = container.find(id);
        if (it == container.end()) {
            return false;
        }

        const bool is_old = old_set.find(id) != old_set.end();
        const bool can_collect = current_collection_full_ || !is_old;

        if (can_collect && marked_set.find(id) == marked_set.end()) {
            container.erase(it);
            ages_map.erase(id);
            old_set.erase(id);
            recovered_in_cycle_++;
        } else if (!is_old) {
            promote_fn(id);
        }

        return false;
    };

    switch (gc_state_) {
        case IncrementalState::SweepArrays:
            if (sweep_index_ == 0 || sweep_phase_ != gc_state_) {
                sweep_keys_.clear();
                for (const auto &kv : arrays_) {
                    sweep_keys_.push_back(kv.first);
                }
                sweep_index_ = 0;
                sweep_phase_ = gc_state_;
            }
            while (work_budget > 0 && sweep_index_ < sweep_keys_.size()) {
                uint32_t id = sweep_keys_[sweep_index_++];
                work_budget--;

                auto it = arrays_.find(id);
                if (it == arrays_.end()) {
                    continue;
                }

                const bool is_old = old_arrays_.find(id) != old_arrays_.end();
                const bool can_collect = current_collection_full_ || !is_old;

                if (can_collect && marked_arrays_.find(id) == marked_arrays_.end()) {
                    arrays_.erase(it);
                    array_ages_.erase(id);
                    old_arrays_.erase(id);
                    recovered_in_cycle_++;
                } else if (!is_old) {
                    ageOrPromoteArray(id);
                }
            }
            if (sweep_index_ >= sweep_keys_.size()) {
                gc_state_ = IncrementalState::SweepObjects;
                sweep_index_ = 0;
            }
            break;

        case IncrementalState::SweepObjects:
            if (sweep_index_ == 0 || sweep_phase_ != gc_state_) {
                sweep_keys_.clear();
                for (const auto &kv : objects_) {
                    sweep_keys_.push_back(kv.first);
                }
                sweep_index_ = 0;
                sweep_phase_ = gc_state_;
            }
            while (work_budget > 0 && sweep_index_ < sweep_keys_.size()) {
                uint32_t id = sweep_keys_[sweep_index_++];
                work_budget--;

                auto it = objects_.find(id);
                if (it == objects_.end()) {
                    continue;
                }

                const bool is_old = old_objects_.find(id) != old_objects_.end();
                const bool can_collect = current_collection_full_ || !is_old;

                if (can_collect && marked_objects_.find(id) == marked_objects_.end()) {
                    objects_.erase(it);
                    object_ages_.erase(id);
                    old_objects_.erase(id);
                    recovered_in_cycle_++;
                } else if (!is_old) {
                    ageOrPromoteObject(id);
                }
            }
            if (sweep_index_ >= sweep_keys_.size()) {
                gc_state_ = IncrementalState::SweepSets;
                sweep_index_ = 0;
            }
            break;

        case IncrementalState::SweepSets:
            if (sweep_index_ == 0 || sweep_phase_ != gc_state_) {
                sweep_keys_.clear();
                for (const auto &kv : sets_) {
                    sweep_keys_.push_back(kv.first);
                }
                sweep_index_ = 0;
                sweep_phase_ = gc_state_;
            }
            while (work_budget > 0 && sweep_index_ < sweep_keys_.size()) {
                uint32_t id = sweep_keys_[sweep_index_++];
                work_budget--;

                auto it = sets_.find(id);
                if (it == sets_.end()) {
                    continue;
                }

                const bool is_old = old_sets_.find(id) != old_sets_.end();
                const bool can_collect = current_collection_full_ || !is_old;

                if (can_collect && marked_sets_.find(id) == marked_sets_.end()) {
                    sets_.erase(it);
                    set_ages_.erase(id);
                    old_sets_.erase(id);
                    recovered_in_cycle_++;
                } else if (!is_old) {
                    ageOrPromoteSet(id);
                }
            }
            if (sweep_index_ >= sweep_keys_.size()) {
                gc_state_ = IncrementalState::SweepClosures;
                sweep_index_ = 0;
            }
            break;

        case IncrementalState::SweepClosures:
            if (sweep_index_ == 0 || sweep_phase_ != gc_state_) {
                sweep_keys_.clear();
                for (const auto &kv : closures_) {
                    sweep_keys_.push_back(kv.first);
                }
                sweep_index_ = 0;
                sweep_phase_ = gc_state_;
            }
            while (work_budget > 0 && sweep_index_ < sweep_keys_.size()) {
                uint32_t id = sweep_keys_[sweep_index_++];
                work_budget--;

                auto it = closures_.find(id);
                if (it == closures_.end()) {
                    continue;
                }

                const bool is_old = old_closures_.find(id) != old_closures_.end();
                const bool can_collect = current_collection_full_ || !is_old;

                if (can_collect && marked_closures_.find(id) == marked_closures_.end()) {
                    closures_.erase(it);
                    closure_ages_.erase(id);
                    old_closures_.erase(id);
                    recovered_in_cycle_++;
                } else if (!is_old) {
                    ageOrPromoteClosure(id);
                }
            }
            if (sweep_index_ >= sweep_keys_.size()) {
                gc_state_ = IncrementalState::SweepStrings;
                sweep_index_ = 0;
            }
            break;

        case IncrementalState::SweepStrings:
            if (sweep_index_ == 0 || sweep_phase_ != gc_state_) {
                sweep_keys_.clear();
                for (const auto &kv : strings_) {
                    sweep_keys_.push_back(kv.first);
                }
                sweep_index_ = 0;
                sweep_phase_ = gc_state_;
            }
            while (work_budget > 0 && sweep_index_ < sweep_keys_.size()) {
                uint32_t id = sweep_keys_[sweep_index_++];
                work_budget--;

                if (marked_strings_.find(id) == marked_strings_.end()) {
                    strings_.erase(id);
                    recovered_in_cycle_++;
                }
            }
            if (sweep_index_ >= sweep_keys_.size()) {
                gc_state_ = IncrementalState::SweepIterators;
                sweep_index_ = 0;
            }
            break;

        case IncrementalState::SweepIterators:
            if (sweep_index_ == 0 || sweep_phase_ != gc_state_) {
                sweep_keys_.clear();
                for (const auto &kv : iterators_) {
                    sweep_keys_.push_back(kv.first);
                }
                sweep_index_ = 0;
                sweep_phase_ = gc_state_;
            }
            while (work_budget > 0 && sweep_index_ < sweep_keys_.size()) {
                uint32_t id = sweep_keys_[sweep_index_++];
                work_budget--;

                if (marked_iterators_.find(id) == marked_iterators_.end()) {
                    iterators_.erase(id);
                    recovered_in_cycle_++;
                }
            }
            if (sweep_index_ >= sweep_keys_.size()) {
                gc_state_ = IncrementalState::SweepRanges;
                sweep_index_ = 0;
            }
            break;

        case IncrementalState::SweepRanges:
            if (sweep_index_ == 0 || sweep_phase_ != gc_state_) {
                sweep_keys_.clear();
                for (const auto &kv : ranges_) {
                    sweep_keys_.push_back(kv.first);
                }
                sweep_index_ = 0;
                sweep_phase_ = gc_state_;
            }
            while (work_budget > 0 && sweep_index_ < sweep_keys_.size()) {
                uint32_t id = sweep_keys_[sweep_index_++];
                work_budget--;

                if (marked_ranges_.find(id) == marked_ranges_.end()) {
                    ranges_.erase(id);
                    recovered_in_cycle_++;
                }
            }
            if (sweep_index_ >= sweep_keys_.size()) {
                gc_state_ = IncrementalState::SweepErrors;
                sweep_index_ = 0;
            }
            break;

        case IncrementalState::SweepErrors:
            if (sweep_index_ == 0 || sweep_phase_ != gc_state_) {
                sweep_keys_.clear();
                for (const auto &kv : errors_) {
                    sweep_keys_.push_back(kv.first);
                }
                sweep_index_ = 0;
                sweep_phase_ = gc_state_;
            }
            while (work_budget > 0 && sweep_index_ < sweep_keys_.size()) {
                uint32_t id = sweep_keys_[sweep_index_++];
                work_budget--;

                if (marked_errors_.find(id) == marked_errors_.end()) {
                    errors_.erase(id);
                    recovered_in_cycle_++;
                }
            }
            if (sweep_index_ >= sweep_keys_.size()) {
                gc_state_ = IncrementalState::SweepEnums;
                sweep_index_ = 0;
            }
            break;

        case IncrementalState::SweepEnums:
            if (sweep_index_ == 0 || sweep_phase_ != gc_state_) {
                sweep_keys_.clear();
                for (const auto &kv : enums_) {
                    sweep_keys_.push_back(kv.first);
                }
                sweep_index_ = 0;
                sweep_phase_ = gc_state_;
            }
            while (work_budget > 0 && sweep_index_ < sweep_keys_.size()) {
                uint32_t id = sweep_keys_[sweep_index_++];
                work_budget--;

                if (marked_enums_.find(id) == marked_enums_.end()) {
                    enums_.erase(id);
                    recovered_in_cycle_++;
                }
            }
            if (sweep_index_ >= sweep_keys_.size()) {
                gc_state_ = IncrementalState::SweepCoroutines;
                sweep_index_ = 0;
            }
            break;

        case IncrementalState::SweepCoroutines:
            if (sweep_index_ == 0 || sweep_phase_ != gc_state_) {
                sweep_keys_.clear();
                for (const auto &kv : coroutines_) {
                    sweep_keys_.push_back(kv.first);
                }
                sweep_index_ = 0;
                sweep_phase_ = gc_state_;
            }
            while (work_budget > 0 && sweep_index_ < sweep_keys_.size()) {
                uint32_t id = sweep_keys_[sweep_index_++];
                work_budget--;

                if (marked_coroutines_.find(id) == marked_coroutines_.end()) {
                    if (auto *co = coroutine(id); co && co->state != Coroutine::Done) {
                        continue;
                    }
                    coroutines_.erase(id);
                    recovered_in_cycle_++;
                }
            }
            if (sweep_index_ >= sweep_keys_.size()) {
                gc_state_ = IncrementalState::SweepThreads;
                sweep_index_ = 0;
            }
            break;

        case IncrementalState::SweepThreads:
            if (sweep_index_ == 0 || sweep_phase_ != gc_state_) {
                sweep_keys_.clear();
                for (const auto &kv : threads_) {
                    sweep_keys_.push_back(kv.first);
                }
                sweep_index_ = 0;
                sweep_phase_ = gc_state_;
            }
            while (work_budget > 0 && sweep_index_ < sweep_keys_.size()) {
                uint32_t id = sweep_keys_[sweep_index_++];
                work_budget--;

                if (marked_threads_.find(id) == marked_threads_.end()) {
                    if (auto *t = thread(id); t && t->isRunning()) {
                        continue;
                    }
                    threads_.erase(id);
                    recovered_in_cycle_++;
                }
            }
            if (sweep_index_ >= sweep_keys_.size()) {
                gc_state_ = IncrementalState::SweepIntervals;
                sweep_index_ = 0;
            }
            break;

        case IncrementalState::SweepIntervals:
            if (sweep_index_ == 0 || sweep_phase_ != gc_state_) {
                sweep_keys_.clear();
                for (const auto &kv : intervals_) {
                    sweep_keys_.push_back(kv.first);
                }
                sweep_index_ = 0;
                sweep_phase_ = gc_state_;
            }
            while (work_budget > 0 && sweep_index_ < sweep_keys_.size()) {
                uint32_t id = sweep_keys_[sweep_index_++];
                work_budget--;

                if (marked_intervals_.find(id) == marked_intervals_.end()) {
                    intervals_.erase(id);
                    recovered_in_cycle_++;
                }
            }
            if (sweep_index_ >= sweep_keys_.size()) {
                gc_state_ = IncrementalState::SweepTimeouts;
                sweep_index_ = 0;
            }
            break;

        case IncrementalState::SweepTimeouts:
            if (sweep_index_ == 0 || sweep_phase_ != gc_state_) {
                sweep_keys_.clear();
                for (const auto &kv : timeouts_) {
                    sweep_keys_.push_back(kv.first);
                }
                sweep_index_ = 0;
                sweep_phase_ = gc_state_;
            }
            while (work_budget > 0 && sweep_index_ < sweep_keys_.size()) {
                uint32_t id = sweep_keys_[sweep_index_++];
                work_budget--;

                if (marked_timeouts_.find(id) == marked_timeouts_.end()) {
                    timeouts_.erase(id);
                    recovered_in_cycle_++;
                }
            }
            if (sweep_index_ >= sweep_keys_.size()) {
                gc_state_ = IncrementalState::SweepChannels;
                sweep_index_ = 0;
            }
            break;

        case IncrementalState::SweepChannels:
            if (sweep_index_ == 0 || sweep_phase_ != gc_state_) {
                sweep_keys_.clear();
                for (const auto &kv : channels_) {
                    sweep_keys_.push_back(kv.first);
                }
                sweep_index_ = 0;
                sweep_phase_ = gc_state_;
            }
            while (work_budget > 0 && sweep_index_ < sweep_keys_.size()) {
                uint32_t id = sweep_keys_[sweep_index_++];
                work_budget--;

                if (marked_channels_.find(id) == marked_channels_.end()) {
                    channels_.erase(id);
                    recovered_in_cycle_++;
                }
            }
            if (sweep_index_ >= sweep_keys_.size()) {
                gc_state_ = IncrementalState::Idle;
                sweep_index_ = 0;
            }
            break;

        default:
            gc_state_ = IncrementalState::Idle;
            break;
    }
}

void GCHeap::completeCollection() {
    const int64_t current_budget = static_cast<int64_t>(allocation_budget_);
    const int64_t recovered = static_cast<int64_t>(recovered_in_cycle_);
    int64_t next_budget = (2 * current_budget) - ((3 * recovered) / 2);
    next_budget = std::clamp<int64_t>(next_budget,
        static_cast<int64_t>(kMinAllocationBudget),
        static_cast<int64_t>(kMaxAllocationBudget));

    allocation_budget_ = static_cast<size_t>(next_budget);
    allocations_since_last_ = 0;
    total_recovered_ += recovered_in_cycle_;
    collections_++;

    mark_worklist_.clear();
    marked_arrays_.clear();
    marked_objects_.clear();
    marked_sets_.clear();
    marked_closures_.clear();
    marked_strings_.clear();
    marked_iterators_.clear();
    marked_ranges_.clear();
    marked_errors_.clear();
    marked_enums_.clear();
    marked_coroutines_.clear();
    marked_threads_.clear();
    marked_intervals_.clear();
    marked_timeouts_.clear();
    marked_channels_.clear();

    root_stack_snapshot_.clear();
    root_locals_snapshot_.clear();
    root_globals_snapshot_.clear();
    root_closures_snapshot_.clear();
    open_local_reader_snapshot_ = {};

    sweep_keys_.clear();
    sweep_index_ = 0;

    if (current_collection_full_) {
        minor_collections_since_full_ = 0;
        current_collection_full_ = false;
    } else {
        minor_collections_since_full_++;
    }

    ::havel::debug("[GC] Collection complete: recovered {} objects, new budget: {}",
        recovered_in_cycle_, allocation_budget_);
}

void GCHeap::ageOrPromoteArray(uint32_t id) {
    auto &age = array_ages_[id];
    if (age < std::numeric_limits<uint8_t>::max()) {
        age++;
    }
    if (age >= promotion_age_threshold_) {
        old_arrays_.insert(id);
    }
}

void GCHeap::ageOrPromoteObject(uint32_t id) {
    auto &age = object_ages_[id];
    if (age < std::numeric_limits<uint8_t>::max()) {
        age++;
    }
    if (age >= promotion_age_threshold_) {
        old_objects_.insert(id);
    }
}

void GCHeap::ageOrPromoteSet(uint32_t id) {
    auto &age = set_ages_[id];
    if (age < std::numeric_limits<uint8_t>::max()) {
        age++;
    }
    if (age >= promotion_age_threshold_) {
        old_sets_.insert(id);
    }
}

void GCHeap::ageOrPromoteClosure(uint32_t id) {
    auto &age = closure_ages_[id];
    if (age < std::numeric_limits<uint8_t>::max()) {
        age++;
    }
    if (age >= promotion_age_threshold_) {
        old_closures_.insert(id);
    }
}

std::shared_ptr<GCHeap::UpvalueCell> GCHeap::createUpvalue(uint32_t index) {
    auto cell = std::make_shared<UpvalueCell>();
    cell->is_open = true;
    cell->open_index = index;
    return cell;
}

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

ChannelRef GCHeap::allocateChannel() {
    const uint32_t id = next_channel_id_++;
    channels_[id] = {};
    return ChannelRef{.id = id};
}

uint32_t GCHeap::allocateThread() {
    const uint32_t id = next_thread_id_++;
    threads_[id] = nullptr;
    return id;
}

uint32_t GCHeap::allocateInterval() {
    const uint32_t id = next_interval_id_++;
    intervals_[id] = nullptr;
    return id;
}

uint32_t GCHeap::allocateTimeout() {
    const uint32_t id = next_timeout_id_++;
    timeouts_[id] = nullptr;
    return id;
}

uint32_t GCHeap::allocateCoroutine(uint32_t function_index, uint32_t chunk_index) {
    const uint32_t id = next_coroutine_id_++;
    Coroutine co;
    co.function_index = function_index;
    co.chunk_index = chunk_index;
    co.ip = 0;
    co.state = Coroutine::Runnable;
    coroutines_[id] = std::move(co);
    return id;
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

GCHeap::Coroutine* GCHeap::coroutine(uint32_t id) {
    auto it = coroutines_.find(id);
    return it == coroutines_.end() ? nullptr : &it->second;
}

const GCHeap::Coroutine* GCHeap::coroutine(uint32_t id) const {
    auto it = coroutines_.find(id);
    return it == coroutines_.end() ? nullptr : &it->second;
}

}
