#pragma once

#include <string>
#include <vector>
#include <optional>

namespace havel::utils {

struct ExecResult {
    int exitCode = -1;
    std::string stdout_output;
    std::string stderr_output;
};

bool execDetached(const std::vector<std::string>& argv);

std::optional<ExecResult> execSync(const std::vector<std::string>& argv);

std::optional<std::string> execCapture(const std::vector<std::string>& argv);

std::vector<pid_t> findProcessesByName(const std::string& name);

bool processExistsByName(const std::string& name);

} // namespace havel::utils
