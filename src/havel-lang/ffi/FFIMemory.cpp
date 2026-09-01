#include "FFIMemory.hpp"
#include "../../utils/Logger.hpp"
#include "FFITypes.hpp"
#include "core/Value.hpp"
#include <cstdlib>
#include <cstring>
#include <limits>

#ifdef HAVE_LIBFFI

namespace havel::ffi {

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
    return ptr;
}

void* FFIMemory::realloc(void* ptr, size_t new_size) {
	if (!ptr) return alloc_bytes(new_size);
	if (new_size == 0) { std::free(ptr); return nullptr; }

	void* new_ptr = std::realloc(ptr, new_size);
	return new_ptr;
}

void FFIMemory::free(void* ptr) {
	if (!ptr) return;
	std::free(ptr);
}

void* FFIMemory::cast(void* ptr, std::shared_ptr<FFIType> new_type) {
    return ptr;
}

void FFIMemory::mark(void* ptr) {
    // No-op without tracking
}

void FFIMemory::sweep() {
    // No-op without tracking
}

void FFIMemory::attach_finalizer(void* ptr, std::function<void(void*)> finalizer) {
    // No-op without tracking
}

void* FFIMemory::to_native(const Value& v, std::shared_ptr<FFIType> type) {
    return nullptr;
}

Value FFIMemory::to_havel(void* ptr, std::shared_ptr<FFIType> type, bool take_ownership) {
    return Value::makeNull();
}

void FFIMemory::dump_stats() {
}

bool FFIMemory::is_valid(void* ptr) {
    return ptr != nullptr;
}

size_t FFIMemory::total_allocated() {
    return 0;
}

size_t FFIMemory::total_used() {
    return 0;
}

} // namespace havel::ffi

#endif // HAVE_LIBFFI