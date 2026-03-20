/*
 * IOService.cpp
 *
 * Pure C++ IO service implementation.
 * No VM, no interpreter, no HavelValue - just system logic.
 */
#include "core/IO.hpp"  // Include full IO definition for method calls (brings in all deps)
#include "IOService.hpp"
#include <thread>
#include <chrono>

namespace havel { namespace host {

IOService::IOService(havel::IO* io) : m_io(io) {}

// =========================================================================
// Key sending operations
// =========================================================================

bool IOService::sendKeys(const std::string& keys) {
    if (!m_io) return false;
    m_io->Send(keys.c_str());
    return true;
}

bool IOService::sendKey(const std::string& key) {
    if (!m_io) return false;
    m_io->SendX11Key(key, true);
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    m_io->SendX11Key(key, false);
    return true;
}

bool IOService::keyDown(const std::string& key) {
    if (!m_io) return false;
    m_io->SendX11Key(key, true);
    return true;
}

bool IOService::keyUp(const std::string& key) {
    if (!m_io) return false;
    m_io->SendX11Key(key, false);
    return true;
}

// =========================================================================
// Key mapping operations
// =========================================================================

bool IOService::map(const std::string& from, const std::string& to) {
    if (!m_io) return false;
    m_io->Map(from, to);
    return true;
}

bool IOService::remap(const std::string& key1, const std::string& key2) {
    if (!m_io) return false;
    m_io->Remap(key1, key2);
    return true;
}

// =========================================================================
// IO control operations
// =========================================================================

void IOService::block() {
    if (m_io) m_io->EmergencyReleaseAllKeys();
}

void IOService::unblock() {
    if (m_io) m_io->UngrabAll();
}

bool IOService::suspend() {
    if (!m_io) return false;
    return m_io->Suspend();
}

bool IOService::resume() {
    if (!m_io) return false;
    if (m_io->isSuspended) {
        return m_io->Suspend();
    }
    return true;
}

bool IOService::isSuspended() const {
    return m_io ? m_io->IsSuspended() : false;
}

void IOService::grab() {
    if (m_io) m_io->EmergencyReleaseAllKeys();
}

void IOService::ungrab() {
    if (m_io) m_io->UngrabAll();
}

void IOService::emergencyRelease() {
    if (m_io) m_io->EmergencyReleaseAllKeys();
}

// =========================================================================
// Key state queries
// =========================================================================

bool IOService::getKeyState(const std::string& key) {
    return m_io ? m_io->GetKeyState(key) : false;
}

bool IOService::isKeyPressed(const std::string& key) {
    return m_io ? m_io->IsKeyPressed(key) : false;
}

bool IOService::isShiftPressed() const {
    return m_io ? m_io->IsShiftPressed() : false;
}

bool IOService::isCtrlPressed() const {
    return m_io ? m_io->IsCtrlPressed() : false;
}

bool IOService::isAltPressed() const {
    return m_io ? m_io->IsAltPressed() : false;
}

bool IOService::isWinPressed() const {
    return m_io ? m_io->IsWinPressed() : false;
}

int IOService::getCurrentModifiers() const {
    return m_io ? m_io->GetCurrentModifiers() : 0;
}

// =========================================================================
// Mouse operations
// =========================================================================

bool IOService::mouseMove(int dx, int dy) {
    return m_io ? m_io->MouseMove(dx, dy) : false;
}

bool IOService::mouseMoveTo(int x, int y, int speed, int accel) {
    return m_io ? m_io->MouseMoveTo(x, y, speed, accel) : false;
}

bool IOService::mouseClick(int button) {
    return m_io ? (m_io->MouseClick(button), true) : false;
}

bool IOService::mouseDoubleClick(int button) {
    if (!m_io) return false;
    m_io->MouseClick(button);
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    m_io->MouseClick(button);
    return true;
}

bool IOService::mousePress(int button) {
    return m_io ? m_io->MouseDown(button) : false;
}

bool IOService::mouseRelease(int button) {
    return m_io ? m_io->MouseUp(button) : false;
}

bool IOService::scroll(double dy, double dx) {
    return m_io ? m_io->Scroll(dy, dx) : false;
}

std::pair<int, int> IOService::getMousePosition() const {
    return m_io ? m_io->GetMousePosition() : std::make_pair(0, 0);
}

void IOService::setMouseSensitivity(double sensitivity) {
    if (m_io) m_io->SetMouseSensitivity(sensitivity);
}

double IOService::getMouseSensitivity() const {
    return m_io ? m_io->GetMouseSensitivity() : 1.0;
}

} } // namespace havel::host
