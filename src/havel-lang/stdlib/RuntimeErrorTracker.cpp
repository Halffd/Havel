#include "RuntimeErrorTracker.hpp"

namespace havel::stdlib {

static size_t &runtimeErrorCountRef() {
  static size_t count = 0;
  return count;
}

static std::vector<std::string> &runtimeErrorsRef() {
  static std::vector<std::string> errors;
  return errors;
}

size_t runtimeErrorCount() { return runtimeErrorCountRef(); }
const std::vector<std::string> &runtimeErrorsList() { return runtimeErrorsRef(); }

void notifyRuntimeError(const std::string &msg) {
  runtimeErrorCountRef()++;
  runtimeErrorsRef().push_back(msg);
}

}
