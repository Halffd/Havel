#include <X11/Xlib.h>
#include <X11/keysym.h>
#include <iostream>
#include <cstring>

// Error handler to catch X errors
int xerrorHandler(Display* display, XErrorEvent* error) {
    char errorText[256];
    XGetErrorText(display, error->error_code, errorText, sizeof(errorText));
    
    std::cerr << "X Error: " << errorText << " (" << (int)error->error_code << ")" << std::endl;
    std::cerr << "  Request code: " << (int)error->request_code << std::endl;
    std::cerr << "  Minor code: " << (int)error->minor_code << std::endl;
    std::cerr << "  Resource ID: " << error->resourceid << std::endl;
    
    return 0;
}

int main() {
    // Open display
    Display* display = XOpenDisplay(NULL);
    if (!display) {
        std::cerr << "Cannot open display" << std::endl;
        return 1;
    }
    
    // Set error handler
    XSetErrorHandler(xerrorHandler);
    
    // Get the root window
    Window root = DefaultRootWindow(display);
    
    KeyCode keycode = 97;
    std::cout << "Using keycode " << (int)keycode << std::endl;
    
    // Try to grab the key
    Status status = XGrabKey(display, keycode, 0, root, True, 
                             GrabModeAsync, GrabModeAsync);
    
    // Force processing of any errors
    XSync(display, False);
    
    if (status != Success) {
        std::cerr << "Failed to grab key (code: " << (int)keycode << ") with modifiers: 0" << std::endl;
        XCloseDisplay(display);
        return 1;
    }
    
    std::cout << "Key grab successful. Press 'a' to test (Ctrl+C to exit)" << std::endl;
    
    // Event loop
    XEvent event;
    while (1) {
        XNextEvent(display, &event);
        
        if (event.type == KeyPress) {
            XKeyEvent* key_event = (XKeyEvent*)&event;
            
            if (key_event->keycode == keycode) {
                std::cout << "Key 'a' was pressed!" << std::endl;
                
                // Get the time of the key press
                std::cout << "  Time: " << key_event->time << std::endl;
                
                // Get state of modifiers
                std::cout << "  Modifier state: " << key_event->state << std::endl;
                
                // Get window information
                std::cout << "  Window: " << key_event->window << std::endl;
            }
        }
    }
    
    // We'll never reach here due to the infinite loop, but for completeness:
    XUngrabKey(display, keycode, 0, root);
    XCloseDisplay(display);
    return 0;
}
