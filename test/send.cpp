#include "x11.h"
#include <xdo.h>
#include <string>
#include <vector>
#include <chrono>
#include <thread>
#include <iostream>

class KeyboardHandler {
public:
    KeyboardHandler(Display* display)
        : xdo(xdo_new(nullptr)), display(display) {}

    ~KeyboardHandler() {
        if (xdo) xdo_free(xdo);
    }

    void send(const std::string& keys, bool blind = false) {
        auto activeModifiers = getActiveModifiers();
        clearActiveModifiers(activeModifiers);

        for (const char& key : keys) {
            pressKey(key, true);  // Key down
            std::this_thread::sleep_for(std::chrono::milliseconds(keyPressDuration));
            pressKey(key, false); // Key up
            std::this_thread::sleep_for(std::chrono::milliseconds(keyDelay));
        }

        restoreModifiers(activeModifiers);
    }

    void sendRaw(const std::string& text) {
        xdo_enter_text_window(xdo, CURRENTWINDOW, text.c_str(), 0);
    }

private:
    xdo_t* xdo;
    Display* display;
    int keyPressDuration = 50; // milliseconds
    int keyDelay = 50;         // milliseconds

    std::vector<charcodemap_t> getActiveModifiers() {
        // Retrieve active modifiers (stubbed for simplicity).
        return {};
    }

    void clearActiveModifiers(const std::vector<charcodemap_t>& modifiers) {
        xdo_clear_active_modifiers(xdo, CURRENTWINDOW, modifiers.data(), modifiers.size());
    }

    void restoreModifiers(const std::vector<charcodemap_t>& modifiers) {
        xdo_set_active_modifiers(xdo, CURRENTWINDOW, modifiers.data(), modifiers.size());
    }

    void pressKey(char key, bool pressed) {
        std::string keySequence(1, key); // Convert char to string
        if (pressed) {
            xdo_send_keysequence_window_down(xdo, CURRENTWINDOW, keySequence.c_str(), 0);
        } else {
            xdo_send_keysequence_window_up(xdo, CURRENTWINDOW, keySequence.c_str(), 0);
        }
    }
};

int main() {
    Display* display = XOpenDisplay(nullptr);
    if (!display) {
        std::cerr << "Unable to open X display\n";
        return 1;
    }

    KeyboardHandler keyboardHandler(display);

    // Example usage
    keyboardHandler.send("Hello, World!"); // Sends keystrokes
    keyboardHandler.sendRaw("This is a test!"); // Sends raw text input

    XCloseDisplay(display);
    return 0;
}

