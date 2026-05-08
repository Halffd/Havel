#include <stdio.h>

#ifdef _WIN32
#define EXPORT __declspec(dllexport)
#else
#define EXPORT
#endif

EXPORT int add(int a, int b) {
    return a + b;
}

EXPORT void hello(const char* name) {
    printf("Hello from DLL, %s!\n", name);
}

EXPORT double multiply(double a, double b) {
    return a * b;
}
