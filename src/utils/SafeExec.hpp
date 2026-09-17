#pragma once

#include "havel-lang/common/Export.hpp"
#include <string>
#include <vector>
#include <optional>

namespace havel::utils {

struct HAVEL_EXPORT ExecResult {
  int exitCode = -1;
  std::string stdout_output;
  std::string stderr_output;
};

HAVEL_EXPORT bool execDetached(const std::vector<std::string>& argv);

HAVEL_EXPORT std::optional<ExecResult> execSync(const std::vector<std::string>& argv);

HAVEL_EXPORT std::optional<std::string> execCapture(const std::vector<std::string>& argv);

HAVEL_EXPORT std::vector<pid_t> findProcessesByName(const std::string& name);

HAVEL_EXPORT bool processExistsByName(const std::string& name);

} // namespace havel::utils
