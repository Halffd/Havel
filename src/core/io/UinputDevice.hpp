/*
 * UinputDevice.hpp - Virtual input device for event injection
 * 
 * Handles uinput device setup and event injection.
 * Separated from EventListener to break monolithic design.
 */
#pragma once

#include <linux/input.h>
#include <vector>
#include <mutex>
#include <atomic>
#include <unordered_set>

namespace havel {

/**
 * UinputDevice - Virtual input device manager
 * 
 * Responsibilities:
 * - Setup uinput device
 * - Send key/mouse events
 * - Batch event injection
 * - Track pressed virtual keys
 */
class UinputDevice {
public:
    UinputDevice();
    ~UinputDevice();
    
    // Initialize uinput device
    bool Setup();
    
    // Send a single event
    void SendEvent(int type, int code, int value);
    
    // Batch operations for reduced syscall overhead
    void BeginBatch();
    void EndBatch();
    
    // Emergency release all keys (for safety)
    void ReleaseAllKeys();
    
    // Check if initialized
    bool IsInitialized() const { return uinputFd >= 0; }
    
    // Get file descriptor (for EventListener integration)
    int GetFd() const { return uinputFd; }

private:
    int uinputFd = -1;
    
    // Track pressed virtual keys for emergency release
    std::unordered_set<int> pressedVirtualKeys;
    mutable std::mutex pressedKeysMutex;
    
    // Batching for reduced syscall overhead
    std::vector<input_event> batchBuffer;
    std::mutex batchMutex;
    std::atomic<bool> batching{false};
    
    static constexpr size_t MAX_BATCH_SIZE = 16;
    static constexpr int BATCH_TIMEOUT_US = 5000;
};

} // namespace havel
