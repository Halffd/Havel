#pragma once

#include <string>
#include <limits.h>  // For PATH_MAX

namespace havel {

// String conversion utilities
std::string ToLower(const std::string& str);
std::string ToUpper(const std::string& str);

// String manipulation utilities
void Trim(std::string& str);
std::string TrimCopy(const std::string& str);
void RemoveChars(std::string& str, const std::string& chars);

// Path utilities
std::string GetExecutablePath();
std::string GetExecutableDir();
std::string GetCurrentDir();

// System utilities
bool IsElevated();
void ElevateProcess();
void SetProcessPriority(int priority);

} // namespace havel 