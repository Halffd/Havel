#include <dlfcn.h>
#include <iostream>
#include <cstdlib>

int main(int argc, char** argv) {
    const char* libPath = argc > 1 ? argv[1] : "./image_extension.so";
    
    std::cout << "Testing extension loading..." << std::endl;
    std::cout << "Loading: " << libPath << std::endl;
    
    void* handle = dlopen(libPath, RTLD_NOW | RTLD_LOCAL);
    if (!handle) {
        std::cerr << "FAILED to load: " << dlerror() << std::endl;
        return 1;
    }
    
    std::cout << "✓ Library loaded successfully" << std::endl;
    
    // Try to get the extension entry point
    void* initFunc = dlsym(handle, "havel_extension_init");
    if (!initFunc) {
        std::cerr << "⚠ Warning: havel_extension_init not found: " << dlerror() << std::endl;
    } else {
        std::cout << "✓ havel_extension_init found" << std::endl;
    }
    
    // List other possible symbols
    void* createFunc = dlsym(handle, "createExtension");
    if (createFunc) {
        std::cout << "✓ createExtension found" << std::endl;
    }
    
    void* versionFunc = dlsym(handle, "extension_version");
    if (versionFunc) {
        std::cout << "✓ extension_version found" << std::endl;
    }
    
    dlclose(handle);
    std::cout << "✓ Test passed - Extension is loadable" << std::endl;
    return 0;
}
