#include "FFIMemory.hpp"
#include "../../utils/Logger.hpp"
#include "FFITypes.hpp"
#include "../core/Value.hpp"
#include <cstdlib>
#include <cstring>
#include <limits>

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
    total_used_ -= it->second.size;
    it->second.size = new_size;
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
        total_used_ -= it->second.size;
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
            total_used_ -= it->second.size;
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
    if (!type) return nullptr;
    size_t sz = FFITypeRegistry::size_of(type);
    void* buf = std::malloc(sz);
    if (!buf) return nullptr;
    std::memset(buf, 0, sz);
    switch (type->kind) {
    case FFITypeKind::BOOL: {
        bool val = v.isInt() ? (v.asInt64() != 0) : (v.asDouble() != 0.0);
        std::memcpy(buf, &val, 1);
        break;
    }
    case FFITypeKind::INT8: {
        int8_t val = v.isInt() ? static_cast<int8_t>(v.asInt64()) : static_cast<int8_t>(v.asDouble());
        std::memcpy(buf, &val, 1);
        break;
    }
    case FFITypeKind::INT16: {
        int16_t val = v.isInt() ? static_cast<int16_t>(v.asInt64()) : static_cast<int16_t>(v.asDouble());
        std::memcpy(buf, &val, 2);
        break;
    }
    case FFITypeKind::INT32: {
        int32_t val = v.isInt() ? static_cast<int32_t>(v.asInt64()) : static_cast<int32_t>(v.asDouble());
        std::memcpy(buf, &val, 4);
        break;
    }
    case FFITypeKind::INT64: {
        int64_t val = v.isInt() ? v.asInt64() : static_cast<int64_t>(v.asDouble());
        std::memcpy(buf, &val, 8);
        break;
    }
    case FFITypeKind::UINT8: {
        uint8_t val = v.isInt() ? static_cast<uint8_t>(v.asInt64()) : static_cast<uint8_t>(v.asDouble());
        std::memcpy(buf, &val, 1);
        break;
    }
    case FFITypeKind::UINT16: {
        uint16_t val = v.isInt() ? static_cast<uint16_t>(v.asInt64()) : static_cast<uint16_t>(v.asDouble());
        std::memcpy(buf, &val, 2);
        break;
    }
    case FFITypeKind::UINT32: {
        uint32_t val = v.isInt() ? static_cast<uint32_t>(v.asInt64()) : static_cast<uint32_t>(v.asDouble());
        std::memcpy(buf, &val, 4);
        break;
    }
    case FFITypeKind::UINT64: {
        uint64_t val = v.isInt() ? static_cast<uint64_t>(v.asInt64()) : static_cast<uint64_t>(v.asDouble());
        std::memcpy(buf, &val, 8);
        break;
    }
    case FFITypeKind::FLOAT32: {
        float val = v.isInt() ? static_cast<float>(v.asInt64()) : static_cast<float>(v.asDouble());
        std::memcpy(buf, &val, 4);
        break;
    }
    case FFITypeKind::FLOAT64: {
        double val = v.isInt() ? static_cast<double>(v.asInt64()) : v.asDouble();
        std::memcpy(buf, &val, 8);
        break;
    }
    case FFITypeKind::POINTER: {
        void* p = nullptr;
        if (v.isPtr()) p = v.asPtr();
        else if (v.isInt()) p = reinterpret_cast<void*>(static_cast<uintptr_t>(v.asInt64()));
        else if (v.isDouble()) p = reinterpret_cast<void*>(static_cast<uintptr_t>(v.asDouble()));
        std::memcpy(buf, &p, sizeof(void*));
        break;
    }
    case FFITypeKind::STRING: {
        const char* s = "";
        if (v.isPtr()) s = static_cast<const char*>(v.asPtr());
        std::memcpy(buf, &s, sizeof(const char*));
        break;
    }
    default:
        std::free(buf);
        return nullptr;
    }
    return buf;
}

Value FFIMemory::to_havel(void* ptr, std::shared_ptr<FFIType> type, bool take_ownership) {
    if (!ptr || !type) return Value::makeNull();
    switch (type->kind) {
    case FFITypeKind::INT8:
        return Value(static_cast<int64_t>(*reinterpret_cast<int8_t*>(ptr)));
    case FFITypeKind::INT16:
        return Value(static_cast<int64_t>(*reinterpret_cast<int16_t*>(ptr)));
    case FFITypeKind::INT32:
        return Value(static_cast<int64_t>(*reinterpret_cast<int32_t*>(ptr)));
    case FFITypeKind::INT64:
        return Value(*reinterpret_cast<int64_t*>(ptr));
    case FFITypeKind::UINT8:
        return Value(static_cast<int64_t>(*reinterpret_cast<uint8_t*>(ptr)));
    case FFITypeKind::UINT16:
        return Value(static_cast<int64_t>(*reinterpret_cast<uint16_t*>(ptr)));
    case FFITypeKind::UINT32:
        return Value(static_cast<int64_t>(*reinterpret_cast<uint32_t*>(ptr)));
    case FFITypeKind::UINT64: {
        uint64_t val = *reinterpret_cast<uint64_t*>(ptr);
        if (val > static_cast<uint64_t>(std::numeric_limits<int64_t>::max())) {
            return Value::makeDouble(static_cast<double>(val));
        }
        return Value(static_cast<int64_t>(val));
    }
    case FFITypeKind::FLOAT32:
        return Value::makeDouble(static_cast<double>(*reinterpret_cast<float*>(ptr)));
    case FFITypeKind::FLOAT64:
        return Value::makeDouble(*reinterpret_cast<double*>(ptr));
    case FFITypeKind::BOOL:
        return Value(static_cast<int64_t>(*reinterpret_cast<bool*>(ptr)));
    case FFITypeKind::STRING:
        return Value::makePtr(*reinterpret_cast<void**>(ptr));
    case FFITypeKind::POINTER:
        return Value::makePtr(*reinterpret_cast<void**>(ptr));
    default:
        return Value::makePtr(ptr);
    }
}

void FFIMemory::dump_stats() {
    std::lock_guard<std::mutex> lock(alloc_mutex_);
    ::havel::info("FFI Memory Stats:");
    ::havel::info(" Allocations: {}", allocations_.size());
    ::havel::info(" Total allocated: {} bytes", total_allocated_);
    ::havel::info(" Total used: {} bytes", total_used_);
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