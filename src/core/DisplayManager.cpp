#include "DisplayManager.hpp"
#include "x11_includes.h"
#include <iostream>
#include <stdexcept>

namespace havel {
    Display* DisplayManager::display = nullptr;
    ::Window DisplayManager::root = 0;
    bool DisplayManager::initialized = false;
    
    void DisplayManager::Initialize() {
        if (!initialized) {
            display = XOpenDisplay(nullptr);
            if (display) {
                root = DefaultRootWindow(display);
                // Register cleanup on exit
                static Cleanup cleanup;
                XSetErrorHandler(X11ErrorHandler);
                XSetIOErrorHandler([](Display* display) -> int {
                    std::cerr << "X11 I/O Error - Display connection lost\n";
                    std::exit(EXIT_FAILURE);
                    return 0;
                });
                initialized = true;
            }
        }
    }
    
    void DisplayManager::Close() {
        if (display) {
            XCloseDisplay(display);
            display = nullptr;
        }
    }
    
    Display* DisplayManager::GetDisplay() {
        return display;
    }
    
    ::Window DisplayManager::GetRootWindow() {
        Initialize();
        if (!display) {
            throw std::runtime_error("No X11 display available");
        }
        return root;
    }
    
    bool DisplayManager::IsInitialized() {
        return display != nullptr;
    }
    
    int DisplayManager::X11ErrorHandler(Display* display, XErrorEvent* event) {
        char errorText[256];
        XGetErrorText(display, event->error_code, errorText, sizeof(errorText));
        std::cerr << "X11 Error: " << errorText << " (" << static_cast<int>(event->error_code) << ")" << std::endl;
        return 0; // Continue execution
    }
    
    DisplayManager::Cleanup::~Cleanup() {
        if (display) {
            XCloseDisplay(display);
            display = nullptr;
        }
    }
} 