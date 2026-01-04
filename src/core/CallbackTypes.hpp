#pragma once
#include <functional>
#include <string>

namespace havel {

// Callback types for IO and EventListener
using AnyKeyPressCallback = std::function<void(const std::string& key)>;

} // namespace havel