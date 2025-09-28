#include "./Util.hpp"

namespace havel {
    void printStackTrace(int len) {
    std::cout << "----------------" << std::endl;
    std::vector<void *> callstack(len);
    int frames = backtrace(callstack.data(), len);
    char **strs = backtrace_symbols(callstack.data(), frames);

    if (strs == nullptr) {
        std::cout << "Failed to get backtrace symbols" << std::endl;
        std::cout << "----------------" << std::endl;
        return;
    }

    for (int i = 0; i < frames; ++i) {
        std::cout << "  " << i << ": " << strs[i] << std::endl;
    }
    std::cout << "----------------" << std::endl;
    free(strs);
    }
} // namespace havel