#ifndef PRINTER_H
#define PRINTER_H

#include <iostream>
#include <sstream>
#include <string>
#include <vector>
#include <unordered_map>
#include <array>
#include "Logger.hpp"

class Printer {
public:
    // Variadic template function for printing with a delimiter
    template <typename... Args>
    void print(const std::string& format, const std::string& delim, Args... args);
    
private:
    // Helper function to format the output
    template <typename T>
    void formatToStream(std::ostringstream& oss, const std::string& format, const std::string& delim, T value);
    
    template <typename T, typename... Args>
    void formatToStream(std::ostringstream& oss, const std::string& format, const std::string& delim, T value, Args... args);

    // Replace format specifiers for various types
    void replaceFormatSpecifiers(std::ostringstream& oss, const std::string& format, int value);
    void replaceFormatSpecifiers(std::ostringstream& oss, const std::string& format, double value);
    void replaceFormatSpecifiers(std::ostringstream& oss, const std::string& format, float value);
    void replaceFormatSpecifiers(std::ostringstream& oss, const std::string& format, long value);
    void replaceFormatSpecifiers(std::ostringstream& oss, const std::string& format, short value);
    void replaceFormatSpecifiers(std::ostringstream& oss, const std::string& format, unsigned int value);
    void replaceFormatSpecifiers(std::ostringstream& oss, const std::string& format, char value);
    void replaceFormatSpecifiers(std::ostringstream& oss, const std::string& format, const char* value);
    void replaceFormatSpecifiers(std::ostringstream& oss, const std::string& format, std::string value);
    void replaceFormatSpecifiers(std::ostringstream& oss, const std::string& format, bool value);
    void replaceFormatSpecifiers(std::ostringstream& oss, const std::string& format, wchar_t value);
    
    // Pointer types
    template <typename T>
    void replaceFormatSpecifiers(std::ostringstream& oss, const std::string& format, T* value);
    
    // Handling std::vector
    template <typename T>
    void replaceFormatSpecifiers(std::ostringstream& oss, const std::string& format, const std::vector<T>& vec);
    
    // Handling std::unordered_map
    template <typename K, typename V>
    void replaceFormatSpecifiers(std::ostringstream& oss, const std::string& format, const std::unordered_map<K, V>& map);
    
    // Handling arrays
    template <typename T, std::size_t N>
    void replaceFormatSpecifiers(std::ostringstream& oss, const std::string& format, const T (&arr)[N]);

    // Byte and Word types
    using byte = unsigned char;
    using word = unsigned short;
    using dword = unsigned long;

    void replaceFormatSpecifiers(std::ostringstream& oss, const std::string& format, byte value);
    void replaceFormatSpecifiers(std::ostringstream& oss, const std::string& format, word value);
    void replaceFormatSpecifiers(std::ostringstream& oss, const std::string& format, dword value);
    #ifdef WINDOWS
    void replaceFormatSpecifiers(std::ostringstream& oss, const std::string& format, HWND hwnd);
    #endif
};

#endif // PRINTER_H