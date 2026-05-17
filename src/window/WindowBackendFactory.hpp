#pragma once
#include "WindowBackend.hpp"
#include <memory>
#include <string>

namespace havel {

class WindowBackendFactory {
public:
  static std::unique_ptr<WindowBackend> Create();
  static std::unique_ptr<WindowBackend> Create(const std::string &backendName);

  static std::string GetDetectedBackendName();
};

} // namespace havel
