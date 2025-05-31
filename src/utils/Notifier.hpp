#pragma once
#include <string>

namespace havel {

class Notifier {
public:
    static void Show(const std::string& message, const std::string& title = "HvC");
    static void Error(const std::string& message, const std::string& title = "Error");
    static void Warning(const std::string& message, const std::string& title = "Warning");
    static void Info(const std::string& message, const std::string& title = "Information");
};

} // namespace H 