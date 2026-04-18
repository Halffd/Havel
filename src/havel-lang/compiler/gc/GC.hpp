#pragma once

#include "../core/BytecodeIR.hpp"
#include "../../runtime/concurrency/Thread.hpp"

#include <algorithm>
#include <cstdint>
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
    friend class VM;
    struct Stats {
        uint64_t heap_size = 0;
        uint64_t object_count = 0;
        uint64_t collections = 0;
        uint64_t last_pause_ns = 0;
        uint64_t total_recovered = 0;
    };

    struct UpvalueCell {
        bool is_open = false;
        uint32_t open_index = 0;
        Value closed_value = nullptr;

        Value get() const { return is_open ? nullptr : closed_value; }
        void set(Value value) { closed_value = value; }
        void close(Value value = nullptr) {
            closed_value = value;
            is_open = false;
        }
        bool isClosed() const { return !is_open; }
    };

    struct RuntimeClosure {
        uint32_t function_index = 0;
        uint32_t chunk_index = 0;
        std::vector<std::shared_ptr<UpvalueCell>> upvalues;
    };

    struct ObjectEntry {
        std::unordered_map<std::string, Value> data;
        std::vector<std::string> insertionOrder;
        bool sorted = true;

        Value *get(const std::string &key) {
            auto it = data.find(key);
            if (it != data.end())
                return &it->second;
            return nullptr;
        }

        void set(const std::string &key, Value value) {
            if (data.find(key) == data.end()) {
                insertionOrder.push_back(key);
            }
            data[key] = std::move(value);
        }

        Value &operator[](const std::string &key) {
            if (data.find(key) == data.end()) {
                insertionOrder.push_back(key);
            }
            return data[key];
        }

        auto begin() { return data.begin(); }
        auto end() { return data.end(); }
        auto begin() const { return data.begin(); }
        auto end() const { return data.end(); }

        auto find(const std::string &key) { return data.find(key); }
        auto find(const std::string &key) const { return data.find(key); }
        size_t size() const { return data.size(); }
        size_t erase(const std::string &key) {
            auto it = std::find(insertionOrder.begin(), insertionOrder.end(), key);
            if (it != insertionOrder.end()) {
                insertionOrder.erase(it);
            }
            return data.erase(key);
        }

        std::vector<std::string> getKeys() const {
            if (sorted) {
                std::vector<std::string> keys;
                for (const auto &[k, _] : data)
                    keys.push_back(k);
                std::sort(keys.begin(), keys.end());
                return keys;
            }
            return insertionOrder;
        }
    };

    struct ArrayEntry {
        std::vector<Value> data;
        bool frozen = false;

        void push_back(const Value &v) { if (!frozen) data.push_back(v); }
        void pop_back() { if (!frozen) data.pop_back(); }
        Value &back() { return data.back(); }
        const Value &back() const { return data.back(); }
        Value &front() { return data.front(); }
        const Value &front() const { return data.front(); }
        size_t size() const { return data.size(); }
        bool empty() const { return data.empty(); }
        void clear() { if (!frozen) data.clear(); }
        Value &operator[](size_t i) { return data[i]; }
        const Value &operator[](size_t i) const { return data[i]; }
        auto begin() { return data.begin(); }
        auto end() { return data.end(); }
        auto begin() const { return data.begin(); }
        auto end() const { return data.end(); }
        auto rbegin() { return data.rbegin(); }
        auto rend() { return data.rend(); }
        auto rbegin() const { return data.rbegin(); }
        auto rend() const { return data.rend(); }
        void reserve(size_t n) { data.reserve(n); }
        void resize(size_t n) { if (!frozen) data.resize(n); }
        void resize(size_t n, const Value &val) { if (!frozen) data.resize(n, val); }
        void insert(typename std::vector<Value>::iterator pos, const Value &v) { if (!frozen) data.insert(pos, v); }
        void insert(typename std::vector<Value>::iterator pos, typename std::vector<Value>::iterator first, typename std::vector<Value>::iterator last) { if (!frozen) data.insert(pos, first, last); }
        template<typename Iter>
        void insert(typename std::vector<Value>::iterator pos, Iter first, Iter last) { if (!frozen) data.insert(pos, first, last); }
        void erase(typename std::vector<Value>::iterator pos) { if (!frozen) data.erase(pos); }
        void assign(typename std::vector<Value>::iterator first, typename std::vector<Value>::iterator last) { if (!frozen) data.assign(first, last); }
    };

    struct Iterator {
        Value iterable;
        size_t index = 0;
        std::vector<std::string> keys;
    };

    struct Range {
        int64_t start = 0;
        int64_t end = 0;
        int64_t step = 1;
    };

    struct EnumType {
        std::string name;
        std::vector<std::string> variantNames;
    };

    struct ErrorObject {
        std::string errorType;
        std::string message;
        std::string stackTrace;
        uint32_t line = 0;
        uint32_t column = 0;
        Value cause;

        ErrorObject() = default;
        ErrorObject(const std::string &type, const std::string &msg,
            const std::string &trace = "", uint32_t ln = 0, uint32_t col = 0)
            : errorType(type), message(msg), stackTrace(trace), line(ln),
              column(col), cause(nullptr) {}
    };

    struct Coroutine {
        enum State { Runnable, Waiting, Done };

        uint32_t function_index = 0;
        uint32_t chunk_index = 0;
        uint32_t ip = 0;
        std::vector<Value> stack;
        std::vector<Value> locals;
        size_t saved_frame_count = 0;
        std::vector<Value> saved_locals;
        State state = Runnable;
        std::vector<Value> yield_values;

        Coroutine() = default;
    };

    void reset();

    ClosureRef allocateClosure(RuntimeClosure closure);
    StringRef allocateString(std::string value);
    std::string *string(uint32_t id);
    const std::string *string(uint32_t id) const;
    std::shared_ptr<UpvalueCell> createUpvalue(uint32_t index);

    ArrayRef allocateArray();
    ObjectRef allocateObject(bool sorted = true);
    SetRef allocateSet();
    RangeRef allocateRange(int64_t start, int64_t end, int64_t step);
    ErrorRef allocateError(const std::string &errorType,
        const std::string &message,
        const std::string &stackTrace = "", uint32_t line = 0,
        uint32_t column = 0);

    uint32_t registerEnumType(const std::string &name,
        const std::vector<std::string> &variants);
    EnumRef allocateEnum(uint32_t typeId, uint32_t tag, size_t payloadCount);

    IteratorRef allocateIterator(const Value &iterable);

    ThreadRef allocateThreadObj(std::shared_ptr<::havel::Thread> thread);
    IntervalRef allocateIntervalObj(std::shared_ptr<::havel::Interval> interval);
    TimeoutRef allocateTimeoutObj(std::shared_ptr<::havel::Timeout> timeout);
    ChannelRef allocateChannel();

    uint32_t allocateThread();
    uint32_t allocateInterval();
    uint32_t allocateTimeout();

    uint32_t allocateCoroutine(uint32_t function_index, uint32_t chunk_index);

    RuntimeClosure *closure(uint32_t id);
    const RuntimeClosure *closure(uint32_t id) const;
    ArrayEntry *array(uint32_t id);
    const ArrayEntry *array(uint32_t id) const;
    ObjectEntry *object(uint32_t id);
    const ObjectEntry *object(uint32_t id) const;
    std::unordered_map<std::string, Value> *set(uint32_t id);
    const std::unordered_map<std::string, Value> *set(uint32_t id) const;
    Range *range(uint32_t id);
    const Range *range(uint32_t id) const;
    Iterator *iterator(uint32_t id);
    const Iterator *iterator(uint32_t id) const;

    ::havel::Thread* thread(uint32_t id);
    const ::havel::Thread* thread(uint32_t id) const;
    ::havel::Interval* interval(uint32_t id);
    const ::havel::Interval* interval(uint32_t id) const;
    ::havel::Timeout* timeout(uint32_t id);
    const ::havel::Timeout* timeout(uint32_t id) const;

    Coroutine *coroutine(uint32_t id);
    const Coroutine *coroutine(uint32_t id) const;

    ErrorObject *error(uint32_t id);
    const ErrorObject *error(uint32_t id) const;

    uint32_t createIterator(const Value &iterable);
    Value iteratorNext(uint32_t id);

    void setAllocationBudget(size_t value);
    size_t allocationBudget() const { return allocation_budget_; }
    bool isCollectionInProgress() const;

    uint64_t pinExternalRoot(const Value &value);
    bool unpinExternalRoot(uint64_t root_id);
    std::optional<Value> externalRoot(uint64_t root_id) const;
    size_t externalRootCount() const { return external_roots_.size(); }
    Stats stats() const;

    void maybeCollectGarbage(
        const std::vector<Value> &stack_values,
        const std::vector<Value> &locals,
        const std::unordered_map<std::string, Value> &globals,
        const std::vector<uint32_t> &active_closure_ids,
        const std::function<std::optional<Value>(uint32_t)>
        &open_local_reader);

    void
    collectGarbage(const std::vector<Value> &stack_values,
        const std::vector<Value> &locals,
        const std::unordered_map<std::string, Value> &globals,
        const std::vector<uint32_t> &active_closure_ids,
        const std::function<std::optional<Value>(uint32_t)>
        &open_local_reader);

    void
    stepGarbageCollection(const std::vector<Value> &stack_values,
        const std::vector<Value> &locals,
        const std::unordered_map<std::string, Value> &globals,
        const std::vector<uint32_t> &active_closure_ids,
        const std::function<std::optional<Value>(uint32_t)> &open_local_reader,
        size_t work_budget = 128);

    void forceFullCollection(
        const std::vector<Value> &stack_values,
        const std::vector<Value> &locals,
        const std::unordered_map<std::string, Value> &globals,
        const std::vector<uint32_t> &active_closure_ids,
        const std::function<std::optional<Value>(uint32_t)> &open_local_reader);

private:
    enum class Generation : uint8_t {
        Young,
        Old,
    };

    enum class IncrementalState : uint8_t {
        Idle,
        Mark,
        SweepArrays,
        SweepObjects,
        SweepSets,
        SweepClosures,
        SweepStrings,
        SweepIterators,
        SweepRanges,
        SweepErrors,
        SweepEnums,
        SweepCoroutines,
        SweepThreads,
        SweepIntervals,
        SweepTimeouts,
        SweepChannels,
    };

    void markValue(const Value &value,
        std::unordered_set<uint32_t> &marked_arrays,
        std::unordered_set<uint32_t> &marked_objects,
        std::unordered_set<uint32_t> &marked_sets,
        std::unordered_set<uint32_t> &marked_closures,
        const std::function<std::optional<Value>(uint32_t)>
        &open_local_reader) const;
    void startIncrementalCollection(
        const std::vector<Value> &stack_values,
        const std::vector<Value> &locals,
        const std::unordered_map<std::string, Value> &globals,
        const std::vector<uint32_t> &active_closure_ids,
        const std::function<std::optional<Value>(uint32_t)>
        &open_local_reader);
    void markReference(const Value &value);
    void markRoots();
    void markStep(size_t &work_budget);
    void sweepStep(size_t &work_budget);
    void completeCollection();
    void ageOrPromoteArray(uint32_t id);
    void ageOrPromoteObject(uint32_t id);
    void ageOrPromoteSet(uint32_t id);
    void ageOrPromoteClosure(uint32_t id);

    void snapshotSweepKeys();

    std::unordered_map<uint32_t, RuntimeClosure> closures_;
    std::unordered_map<uint32_t, std::string> strings_;
    std::unordered_map<uint32_t, ArrayEntry> arrays_;
    std::unordered_map<uint32_t, ObjectEntry> objects_;
    std::unordered_map<uint32_t, std::unordered_map<std::string, Value>> sets_;
    std::unordered_map<uint32_t, Range> ranges_;
    std::unordered_map<uint32_t, ErrorObject> errors_;
    std::unordered_map<uint32_t, std::pair<uint32_t, std::vector<Value>>> enums_;
    std::unordered_map<uint32_t, Iterator> iterators_;

    std::unordered_map<uint32_t, std::shared_ptr<::havel::Thread>> threads_;
    std::unordered_map<uint32_t, std::shared_ptr<::havel::Interval>> intervals_;
    std::unordered_map<uint32_t, std::shared_ptr<::havel::Timeout>> timeouts_;
    std::unordered_map<uint32_t, std::vector<Value>> channels_;

    std::unordered_map<uint32_t, Coroutine> coroutines_;

    std::vector<EnumType> enumTypes_;

    uint32_t next_closure_id_ = 1;
    uint32_t next_string_id_ = 1;
    uint32_t next_array_id_ = 1;
    uint32_t next_object_id_ = 1;
    uint32_t next_set_id_ = 1;
    uint32_t next_range_id_ = 1;
    uint32_t next_error_id_ = 1;
    uint32_t next_iterator_id_ = 1;
    uint32_t next_thread_id_ = 1;
    uint32_t next_interval_id_ = 1;
    uint32_t next_timeout_id_ = 1;
    uint32_t next_channel_id_ = 1;
    uint32_t next_coroutine_id_ = 1;

    size_t allocation_budget_ = 1024;
    size_t allocations_since_last_ = 0;
    size_t recovered_in_cycle_ = 0;

    std::unordered_map<uint64_t, Value> external_roots_;
    uint64_t next_external_root_id_ = 1;
    uint64_t collections_ = 0;
    uint64_t last_pause_ns_ = 0;
    uint64_t total_recovered_ = 0;

    IncrementalState gc_state_ = IncrementalState::Idle;
    bool collection_requested_ = false;
    bool current_collection_full_ = false;
    size_t minor_collections_since_full_ = 0;
    static constexpr size_t full_collection_interval_ = 8;
    static constexpr uint8_t promotion_age_threshold_ = 2;

    std::vector<Value> mark_worklist_;
    std::unordered_set<uint32_t> marked_arrays_;
    std::unordered_set<uint32_t> marked_objects_;
    std::unordered_set<uint32_t> marked_sets_;
    std::unordered_set<uint32_t> marked_closures_;
    std::unordered_set<uint32_t> marked_strings_;
    std::unordered_set<uint32_t> marked_iterators_;
    std::unordered_set<uint32_t> marked_ranges_;
    std::unordered_set<uint32_t> marked_errors_;
    std::unordered_set<uint32_t> marked_enums_;
    std::unordered_set<uint32_t> marked_coroutines_;
    std::unordered_set<uint32_t> marked_threads_;
    std::unordered_set<uint32_t> marked_intervals_;
    std::unordered_set<uint32_t> marked_timeouts_;
    std::unordered_set<uint32_t> marked_channels_;

    std::unordered_map<uint32_t, uint8_t> array_ages_;
    std::unordered_map<uint32_t, uint8_t> object_ages_;
    std::unordered_map<uint32_t, uint8_t> set_ages_;
    std::unordered_map<uint32_t, uint8_t> closure_ages_;
    std::unordered_set<uint32_t> old_arrays_;
    std::unordered_set<uint32_t> old_objects_;
    std::unordered_set<uint32_t> old_sets_;
    std::unordered_set<uint32_t> old_closures_;

    std::vector<Value> root_stack_snapshot_;
    std::vector<Value> root_locals_snapshot_;
    std::unordered_map<std::string, Value> root_globals_snapshot_;
    std::vector<uint32_t> root_closures_snapshot_;
    std::function<std::optional<Value>(uint32_t)> open_local_reader_snapshot_;

    std::vector<uint32_t> sweep_keys_;
    size_t sweep_index_ = 0;
    IncrementalState sweep_phase_ = IncrementalState::Idle;
};

}
