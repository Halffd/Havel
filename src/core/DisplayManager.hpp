#pragma once
#include "x11_includes.h"
#include <iostream>

namespace havel {

class DisplayManager {
public:
    static void Initialize();
    static Display* GetDisplay();
    static ::Window GetRootWindow();
    static void Close();
    static bool IsInitialized();

    // X11 error handler
    static int X11ErrorHandler(Display* display, XErrorEvent* event);

private:
    static Display* display;  // Declaration only - definition must be in a .cpp file
    static ::Window root;
    static bool initialized;

    struct Cleanup {
        ~Cleanup();
    };

    // RAII wrapper for X11 resources
    template<typename T, auto Deleter>
    class X11Resource {
    public:
        X11Resource(T resource = nullptr) : resource(resource) {}
        ~X11Resource() { if(resource) Deleter(resource); }
        
        operator T() const { return resource; }
        T* operator&() { return &resource; }
        
    private:
        T resource;
    };

    // Usage example
    using XWindow = X11Resource<Window, XDestroyWindow>;
    using XColormap = X11Resource<Colormap, XFreeColormap>;
};

} // namespace H 