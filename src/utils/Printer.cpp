#include "Printer.hpp"
#include "Logger.hpp"
#include <sstream>
#include "types.hpp"
using namespace havel;
// Variadic template function for printing with a delimiter
template <typename... Args>
void Printer::print(const std::string& format, const std::string& prefix, Args... args) {
    std::ostringstream oss;
    oss << prefix;
    processArgs(oss, format, args...);
    std::string out = oss.str();
    info(out);  // Print with newline at the end
}

template<typename T, typename... Rest>
void Printer::processArgs(std::ostringstream& oss, const std::string& format, T value, Rest... rest) {
    size_t pos = format.find("{}");
    if (pos != std::string::npos) {
        oss << format.substr(0, pos);
        replaceFormatSpecifiers(oss, format, value);
        processArgs(oss, format.substr(pos + 2), rest...);
    } else {
        oss << format;
    }
}

template<typename T>
void Printer::processArgs(std::ostringstream& oss, const std::string& format, T value) {
    size_t pos = format.find("{}");
    if (pos != std::string::npos) {
        oss << format.substr(0, pos);
        replaceFormatSpecifiers(oss, format, value);
        oss << format.substr(pos + 2);
    } else {
        oss << format;
    }
}

// Replace format specifiers for various types
void Printer::replaceFormatSpecifiers(std::ostringstream& oss, const std::string&, int value) {
    oss << value;
}

void Printer::replaceFormatSpecifiers(std::ostringstream& oss, const std::string&, double value) {
    oss << value;
}

void Printer::replaceFormatSpecifiers(std::ostringstream& oss, const std::string&, float value) {
    oss << value;
}

void Printer::replaceFormatSpecifiers(std::ostringstream& oss, const std::string&, long value) {
    oss << value;
}

void Printer::replaceFormatSpecifiers(std::ostringstream& oss, const std::string&, short value) {
    oss << value;
}

void Printer::replaceFormatSpecifiers(std::ostringstream& oss, const std::string&, unsigned int value) {
    oss << value;
}

void Printer::replaceFormatSpecifiers(std::ostringstream& oss, const std::string&, char value) {
    oss << value;
}

void Printer::replaceFormatSpecifiers(std::ostringstream& oss, const std::string&, const char* value) {
    oss << value;
}

void Printer::replaceFormatSpecifiers(std::ostringstream& oss, const std::string&, std::string value) {
    oss << value;
}

void Printer::replaceFormatSpecifiers(std::ostringstream& oss, const std::string&, bool value) {
    oss << (value ? "true" : "false");
}

void Printer::replaceFormatSpecifiers(std::ostringstream& oss, const std::string&, wchar_t value) {
    oss << static_cast<int>(value);
}

void Printer::replaceFormatSpecifiers(std::ostringstream& oss, const std::string&, unsigned char value) {
    oss << static_cast<unsigned int>(value);
}

// Handling std::vector
template <typename T>
void Printer::replaceFormatSpecifiers(std::ostringstream& oss, const std::string&, const std::vector<T>& vec) {
    oss << "[";
    for (size_t i = 0; i < vec.size(); ++i) {
        oss << vec[i];
        if (i < vec.size() - 1) {
            oss << ", ";
        }
    }
    oss << "]";
}

// Handling std::unordered_map
template <typename K, typename V>
void Printer::replaceFormatSpecifiers(std::ostringstream& oss, const std::string&, const std::unordered_map<K, V>& map) {
    oss << "{";
    for (auto it = map.begin(); it != map.end(); ++it) {
        oss << it->first << ": " << it->second;
        if (std::next(it) != map.end()) {
            oss << ", ";
        }
    }
    oss << "}";
}

// Handling arrays
template <typename T, std::size_t N>
void Printer::replaceFormatSpecifiers(std::ostringstream& oss, const std::string&, const T (&arr)[N]) {
    oss << "[";
    for (size_t i = 0; i < N; ++i) {
        oss << arr[i];
        if (i < N - 1) {
            oss << ", ";
        }
    }
    oss << "]";
}

#ifdef WINDOWS
// HWND type simulation (using void pointer for demonstration)
void Printer::replaceFormatSpecifiers(std::ostringstream& oss, const std::string&, HWND hwnd) {
    oss << hwnd; // Print HWND as pointer address
}
#endif