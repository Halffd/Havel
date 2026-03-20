/*
 * IOService.hpp
 *
 * Pure C++ IO service - no VM, no interpreter, no HavelValue.
 * This is the business logic layer for IO operations.
 */
#pragma once

#include <string>
#include <utility>

namespace havel { class IO; }  // Forward declaration

namespace havel::host {

/**
 * IOService - Pure IO business logic
 *
 * Provides system-level IO operations without any language runtime coupling.
 * All methods return simple C++ types (bool, int, string, etc.)
 */
class IOService {
public:
    explicit IOService(havel::IO* io);
    ~IOService() = default;

    // =========================================================================
    // Key sending operations
    // =========================================================================

    /// Send a string of keys
    /// @param keys Key string to send
    /// @return true on success
    bool sendKeys(const std::string& keys);

    /// Send a single key press (down + up)
    /// @param key Key name
    /// @return true on success
    bool sendKey(const std::string& key);

    /// Press and hold a key
    /// @param key Key name
    /// @return true on success
    bool keyDown(const std::string& key);

    /// Release a key
    /// @param key Key name
    /// @return true on success
    bool keyUp(const std::string& key);

    // =========================================================================
    // Key mapping operations
    // =========================================================================

    /// Map one key to another
    /// @param from Source key
    /// @param to Target key
    /// @return true on success
    bool map(const std::string& from, const std::string& to);

    /// Remap one key to another (bidirectional)
    /// @param key1 First key
    /// @param key2 Second key
    /// @return true on success
    bool remap(const std::string& key1, const std::string& key2);

    // =========================================================================
    // IO control operations
    // =========================================================================

    /// Block all input (emergency release)
    void block();

    /// Unblock input (ungrab all)
    void unblock();

    /// Suspend IO
    /// @return true if suspended
    bool suspend();

    /// Resume IO
    /// @return true if resumed
    bool resume();

    /// Check if IO is suspended
    /// @return true if suspended
    bool isSuspended() const;

    /// Grab input (emergency release)
    void grab();

    /// Ungrab input
    void ungrab();

    /// Emergency release all keys
    void emergencyRelease();

    // =========================================================================
    // Key state queries
    // =========================================================================

    /// Get state of a specific key
    /// @param key Key name
    /// @return true if pressed
    bool getKeyState(const std::string& key);

    /// Check if a key is currently pressed
    /// @param key Key name
    /// @return true if pressed
    bool isKeyPressed(const std::string& key);

    /// Check if shift is pressed
    /// @return true if shift is pressed
    bool isShiftPressed() const;

    /// Check if control is pressed
    /// @return true if control is pressed
    bool isCtrlPressed() const;

    /// Check if alt is pressed
    /// @return true if alt is pressed
    bool isAltPressed() const;

    /// Check if windows/super key is pressed
    /// @return true if win is pressed
    bool isWinPressed() const;

    /// Get current modifier mask
    /// @return modifier mask value
    int getCurrentModifiers() const;

    // =========================================================================
    // Mouse operations
    // =========================================================================

    /// Move mouse relatively
    /// @param dx Delta X
    /// @param dy Delta Y
    /// @return true on success
    bool mouseMove(int dx, int dy);

    /// Move mouse to absolute position
    /// @param x X coordinate
    /// @param y Y coordinate
    /// @param speed Movement speed (optional)
    /// @param accel Acceleration (optional)
    /// @return true on success
    bool mouseMoveTo(int x, int y, int speed = 1, int accel = 0);

    /// Click mouse button
    /// @param button Button number (1=left, 2=right, 3=middle)
    /// @return true on success
    bool mouseClick(int button);

    /// Double click mouse button
    /// @param button Button number
    /// @return true on success
    bool mouseDoubleClick(int button);

    /// Press and hold mouse button
    /// @param button Button number
    /// @return true on success
    bool mousePress(int button);

    /// Release mouse button
    /// @param button Button number
    /// @return true on success
    bool mouseRelease(int button);

    /// Scroll mouse wheel
    /// @param dy Vertical scroll amount
    /// @param dx Horizontal scroll amount
    /// @return true on success
    bool scroll(double dy, double dx = 0.0);

    /// Get current mouse position
    /// @return pair of (x, y) coordinates
    std::pair<int, int> getMousePosition() const;

    /// Set mouse sensitivity
    /// @param sensitivity Sensitivity value
    void setMouseSensitivity(double sensitivity);

    /// Get mouse sensitivity
    /// @return sensitivity value
    double getMouseSensitivity() const;

private:
    havel::IO* m_io;  // Non-owning pointer to core IO system
};

} // namespace havel::host
