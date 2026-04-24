#include "FFIMemory.hpp"
#include "../../utils/Logger.hpp"
#include "FFITypes.hpp"
#include "../core/Value.hpp"
#include <cstdlib>
#include <cstring>
#include <iostream>

#ifdef HAVE_LIBFFI

namespace havel::ffi {

std::unordered_map<void*, Allocation> FFIMemory::allocations_;
std::mutex FFIMemory::alloc_mutex_;
size_t FFIMemory::total_allocated_ = 0;
size_t FFIMemory::total_used_ = 0;

void* FFIMemory::alloc(std::shared_ptr<FFIType> type) {
    if (!type) return nullptr;
    size_t size = FFITypeRegistry::size_of(type);
    return alloc_bytes(size);
}

void* FFIMemory::alloc_bytes(size_t size) {
    if (size == 0) return nullptr;
    
    void* ptr = std::malloc(size);
    if (!ptr) return nullptr;
    
    std::memset(ptr, 0, size);
    
    std::lock_guard<std::mutex> lock(alloc_mutex_);
    Allocation& a = allocations_[ptr];
    a.ptr = ptr;
    a.size = size;
    a.is_managed = false;
    total_allocated_ += size;
    total_used_ += size;
    
    return ptr;
}

void* FFIMemory::realloc(void* ptr, size_t new_size) {
    if (!ptr) return alloc_bytes(new_size);
    if (new_size == 0) { free(ptr); return nullptr; }
    
    std::lock_guard<std::mutex> lock(alloc_mutex_);
    auto it = allocations_.find(ptr);
    if (it == allocations_.end()) {
        return std::realloc(ptr, new_size);
    }
    
    void* new_ptr = std::realloc(ptr, new_size);
    if (new_ptr && new_ptr != ptr) {
        total_used_ -= it->second.size();
        it->second.size() = new_size;
        total_used_ += new_size;
    }
    return new_ptr;
}

void FFIMemory::free(void* ptr) {
    if (!ptr) return;
    
    std::lock_guard<std::mutex> lock(alloc_mutex_);
    auto it = allocations_.find(ptr);
    if (it != allocations_.end()) {
        if (it->second.finalizer) {
            it->second.finalizer(ptr);
        }
        total_used_ -= it->second.size();
        allocations_.erase(it);
    }
    std::free(ptr);
}

void* FFIMemory::cast(void* ptr, std::shared_ptr<FFIType> new_type) {
    return ptr;
}

void FFIMemory::mark(void* ptr) {
    std::lock_guard<std::mutex> lock(alloc_mutex_);
    auto it = allocations_.find(ptr);
    if (it != allocations_.end()) {
        it->second.gc_mark = 1;
    }
}

void FFIMemory::sweep() {
    std::lock_guard<std::mutex> lock(alloc_mutex_);
    for (auto it = allocations_.begin(); it != allocations_.end(); ) {
        if (it->second.gc_mark == 0) {
            total_used_ -= it->second.size();
            std::free(it->first);
            it = allocations_.erase(it);
        } else {
            it->second.gc_mark = 0;
            ++it;
        }
    }
}

void FFIMemory::attach_finalizer(void* ptr, std::function<void(void*)> finalizer) {
    std::lock_guard<std::mutex> lock(alloc_mutex_);
    auto it = allocations_.find(ptr);
    if (it != allocations_.end()) {
        it->second.finalizer = finalizer;
    }
}

void* FFIMemory::to_native(const Value& v, std::shared_ptr<FFIType> type) {
    return nullptr;
}

Value FFIMemory::to_havel(void* ptr, std::shared_ptr<FFIType> type, bool take_ownership) {
    if (!ptr || !type) return Value::makeNull();
    
    switch (type->kind) {
        case FFITypeKind::INT8: 
        case FFITypeKind::INT16: 
        case FFITypeKind::INT32: 
        case FFITypeKind::INT64:
            return Value(*reinterpret_cast<int64_t*>(ptr));
        case FFITypeKind::UINT8: 
        case FFITypeKind::UINT16: 
        case FFITypeKind::UINT32: 
        case FFITypeKind::UINT64:
            return Value(*reinterpret_cast<uint64_t*>(ptr));
        case FFITypeKind::FLOAT32:
            return Value::makeDouble(*reinterpret_cast<float*>(ptr));
        case FFITypeKind::FLOAT64:
            return Value::makeDouble(*reinterpret_cast<double*>(ptr));
        case FFITypeKind::STRING:
            return Value::makePtr(ptr);
        case FFITypeKind::POINTER:
            return Value::makePtr(ptr);
        default:
            return Value::makePtr(ptr);
    }
}

void FFIMemory::dump_stats() {
    std::lock_guard<std::mutex> lock(alloc_mutex_);
    havel::info("FFI Memory Stats:");
    havel::info(" Allocations: {}", allocations_.size());
    havel::info(" Total allocated: {} bytes", total_allocated_);
    havel::info(" Total used: {} bytes", total_used_);
}

bool FFIMemory::is_valid(void* ptr) {
    std::lock_guard<std::mutex> lock(alloc_mutex_);
    return allocations_.find(ptr) != allocations_.end();
}

size_t FFIMemory::total_allocated() {
    std::lock_guard<std::mutex> lock(alloc_mutex_);
    return total_allocated_;
}

size_t FFIMemory::total_used() {
    std::lock_guard<std::mutex> lock(alloc_mutex_);
    return total_used_;
}

} // namespace havel::ffi

#endif // HAVE_LIBFFI