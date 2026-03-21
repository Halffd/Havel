/*
 * AppService.cpp
 *
 * Application service implementation.
 */
#include "AppService.hpp"
#include <cstdlib>
#include <unistd.h>

namespace havel::host {

AppService::AppService() {
}

AppService::~AppService() {
}

std::string AppService::getName() {
    return "havel";
}

std::string AppService::getVersion() {
    return "1.0.0";
}

std::string AppService::getDescription() {
    return "Havel Window Manager";
}

std::vector<std::string> AppService::getArgs() {
    // Note: Can't access argc/argv from here
    // Would need to be set during initialization
    return {};
}

std::string AppService::getArg(int index) {
    auto args = getArgs();
    if (index < 0 || index >= static_cast<int>(args.size())) {
        return "";
    }
    return args[index];
}

int AppService::getArgCount() {
    return static_cast<int>(getArgs().size());
}

bool AppService::hasArg(const std::string& arg) {
    auto args = getArgs();
    for (const auto& a : args) {
        if (a == arg) return true;
    }
    return false;
}

void AppService::exit(int code) {
    std::exit(code);
}

void AppService::restart() {
    // Can't easily restart from here
    // Would need launcher support
}

std::string AppService::getEnv(const std::string& name) {
    const char* value = std::getenv(name.c_str());
    return value ? value : "";
}

bool AppService::setEnv(const std::string& name, const std::string& value) {
    return setenv(name.c_str(), value.c_str(), 1) == 0;
}

std::vector<std::pair<std::string, std::string>> AppService::getAllEnv() {
    std::vector<std::pair<std::string, std::string>> result;
    
    extern char** environ;
    for (char** env = environ; *env; ++env) {
        std::string var(*env);
        size_t pos = var.find('=');
        if (pos != std::string::npos) {
            result.emplace_back(var.substr(0, pos), var.substr(pos + 1));
        }
    }
    
    return result;
}

int AppService::getPid() {
    return getpid();
}

int AppService::getPpid() {
    return getppid();
}

std::string AppService::getCwd() {
    char cwd[1024];
    if (getcwd(cwd, sizeof(cwd)) != nullptr) {
        return cwd;
    }
    return "";
}

} // namespace havel::host
