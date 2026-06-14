#pragma once

#include <string>
#include <vector>
#include <optional>

namespace havel::utils {

struct __attribute__((visibility("default"))) ExecResult {
  int exitCode = -1;
  std::string stdout_output;
  std::string stderr_output;
};

__attribute__((visibility("default"))) bool execDetached(const std::vector<std::string>& argv);

__attribute__((visibility("default"))) std::optional<ExecResult> execSync(const std::vector<std::string>& argv);

__attribute__((visibility("default"))) std::optional<std::string> execCapture(const std::vector<std::string>& argv);

__attribute__((visibility("default"))) std::vector<pid_t> findProcessesByName(const std::string& name);

__attribute__((visibility("default"))) bool processExistsByName(const std::string& name);

} // namespace havel::utils
