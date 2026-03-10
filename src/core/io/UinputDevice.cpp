/*
 * UinputDevice.cpp - Virtual input device for event injection
 * 
 * Handles uinput device setup and event injection.
 * Separated from EventListener to break monolithic design.
 */
#include "core/io/UinputDevice.hpp"
#include "utils/Logger.hpp"
#include <fcntl.h>
#include <linux/uinput.h>
#include <sys/ioctl.h>
#include <unistd.h>
#include <cstring>

namespace havel {

UinputDevice::UinputDevice() = default;

UinputDevice::~UinputDevice() {
    if (uinputFd >= 0) {
        ioctl(uinputFd, UI_DEV_DESTROY);
        close(uinputFd);
        uinputFd = -1;
    }
}

bool UinputDevice::Setup() {
    uinputFd = open("/dev/uinput", O_WRONLY | O_NONBLOCK);
    if (uinputFd < 0) {
        error("Failed to open /dev/uinput: {}", strerror(errno));
        return false;
    }

    // Enable event types
    ioctl(uinputFd, UI_SET_EVBIT, EV_KEY);
    ioctl(uinputFd, UI_SET_EVBIT, EV_SYN);
    ioctl(uinputFd, UI_SET_EVBIT, EV_REL);
    ioctl(uinputFd, UI_SET_EVBIT, EV_ABS);

    // Enable all key codes
    for (int i = 0; i < KEY_MAX; i++) {
        ioctl(uinputFd, UI_SET_KEYBIT, i);
    }

    // Enable mouse buttons
    for (int i = BTN_MOUSE; i < BTN_DIGI; i++) {
        ioctl(uinputFd, UI_SET_KEYBIT, i);
    }

    // Enable relative axes
    ioctl(uinputFd, UI_SET_RELBIT, REL_X);
    ioctl(uinputFd, UI_SET_RELBIT, REL_Y);
    ioctl(uinputFd, UI_SET_RELBIT, REL_WHEEL);
    ioctl(uinputFd, UI_SET_RELBIT, REL_HWHEEL);

    // Setup device
    struct uinput_user_dev usetup = {};
    std::snprintf(usetup.name, UINPUT_MAX_NAME_SIZE, "havel-virtual-device");
    usetup.id.bustype = BUS_USB;
    usetup.id.vendor = 0x1234;
    usetup.id.product = 0x5678;
    usetup.id.version = 1;

    if (ioctl(uinputFd, UI_DEV_SETUP, &usetup) < 0) {
        error("Failed to setup uinput device: {}", strerror(errno));
        close(uinputFd);
        uinputFd = -1;
        return false;
    }

    if (ioctl(uinputFd, UI_DEV_CREATE) < 0) {
        error("Failed to create uinput device: {}", strerror(errno));
        close(uinputFd);
        uinputFd = -1;
        return false;
    }

    info("uinput device initialized (fd={})", uinputFd);
    return true;
}

void UinputDevice::SendEvent(int type, int code, int value) {
    if (uinputFd < 0) {
        error("Cannot send event: uinput not initialized (fd={})", uinputFd);
        return;
    }

    // Track pressed keys for emergency release
    if (type == EV_KEY) {
        std::lock_guard<std::mutex> lock(pressedKeysMutex);
        if (value == 1) {
            pressedVirtualKeys.insert(code);
        } else if (value == 0) {
            pressedVirtualKeys.erase(code);
        }
    }

    // Check if we're in batching mode
    if (batching.load(std::memory_order_relaxed)) {
        std::lock_guard<std::mutex> lock(batchMutex);
        
        input_event ev = {};
        ev.type = type;
        ev.code = code;
        ev.value = value;
        batchBuffer.push_back(ev);

        // Auto-flush if batch is full
        if (batchBuffer.size() >= MAX_BATCH_SIZE) {
            // Need to flush - but we're already holding the lock
            // So we'll let EndBatch() handle it
        }
        return;
    }

    // Send immediately
    input_event ev = {};
    ev.type = type;
    ev.code = code;
    ev.value = value;

    ssize_t written = write(uinputFd, &ev, sizeof(ev));
    if (written < 0) {
        error("Failed to write to uinput: {} (fd={})", strerror(errno), uinputFd);
        return;
    }

    // Send SYN_REPORT to flush
    input_event syn = {};
    syn.type = EV_SYN;
    syn.code = SYN_REPORT;
    syn.value = 0;

    ssize_t syn_written = write(uinputFd, &syn, sizeof(syn));
    if (syn_written < 0) {
        error("Failed to write SYN_REPORT: {} (fd={})", strerror(errno), uinputFd);
    }
}

void UinputDevice::BeginBatch() {
    std::lock_guard<std::mutex> lock(batchMutex);
    batchBuffer.clear();
    batching.store(true, std::memory_order_relaxed);
}

void UinputDevice::EndBatch() {
    if (!batching.load(std::memory_order_relaxed)) {
        return;
    }

    batching.store(false, std::memory_order_relaxed);

    // Copy batch buffer under lock
    std::vector<input_event> batchCopy;
    {
        std::lock_guard<std::mutex> lock(batchMutex);
        if (batchBuffer.empty()) {
            return;
        }
        batchCopy = std::move(batchBuffer);
    }

    if (uinputFd < 0) {
        error("Cannot flush batch: uinput not initialized (fd={})", uinputFd);
        return;
    }

    // Write all events in batch
    ssize_t written = write(uinputFd, batchCopy.data(),
                           batchCopy.size() * sizeof(input_event));
    if (written < 0) {
        error("Failed to flush batch: {} (fd={})", strerror(errno), uinputFd);
        return;
    }

    // Send final SYN_REPORT
    input_event syn = {};
    syn.type = EV_SYN;
    syn.code = SYN_REPORT;
    syn.value = 0;

    write(uinputFd, &syn, sizeof(syn));
}

void UinputDevice::ReleaseAllKeys() {
    std::unordered_set<int> keysToRelease;
    {
        std::lock_guard<std::mutex> lock(pressedKeysMutex);
        keysToRelease = pressedVirtualKeys;
        pressedVirtualKeys.clear();
    }

    for (int code : keysToRelease) {
        SendEvent(EV_KEY, code, 0);
    }

    info("Emergency released {} virtual keys", keysToRelease.size());
}

} // namespace havel
