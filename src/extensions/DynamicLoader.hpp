#pragma once
#include <string>

namespace havel {

class DynamicLoader {
public:
  static bool loadLibrary(const std::string& path) { return false; }
  static void* getSymbol(const std::string& name) { return nullptr; }
  static void unloadAll() {}
};

} // namespace havel
