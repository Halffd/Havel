#pragma once

#include <cstdint>
#include <memory>
#include <functional>
#include <mutex>
#include <unordered_map>

namespace havel::ffi {

struct FFIType;

struct Allocation {
    void* ptr = nullptr;
    std::shared_ptr<FFIType> type;
    size_t size = 0;
    bool is_managed = false;
    uint64_t gc_mark = 0;
    std::function<void(void*)> finalizer;
};

class FFIMemory {
public:
    static void* alloc(std::shared_ptr<FFIType> type);
    static void* alloc_bytes(size_t size);
    static void* realloc(void* ptr, size_t new_size);
    static void free(void* ptr);
    
    static void* cast(void* ptr, std::shared_ptr<FFIType> new_type);
    
    static void mark(void* ptr);
    static void sweep();
    static void attach_finalizer(void* ptr, std::function<void(void*)> finalizer);
    
    static void* to_native(const class Value& v, std::shared_ptr<FFIType> type);
    static class Value to_havel(void* ptr, std::shared_ptr<FFIType> type, bool take_ownership = false);
    
    static void dump_stats();
    static bool is_valid(void* ptr);
    static size_t total_allocated();
    static size_t total_used();
    
private:
    static std::unordered_map<void*, Allocation> allocations_;
    static std::mutex alloc_mutex_;
    static size_t total_allocated_;
    static size_t total_used_;
};

}