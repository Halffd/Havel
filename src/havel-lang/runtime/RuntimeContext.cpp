/*
 * RuntimeContext.cpp
 *
 * Runtime context implementation.
 */
#include "RuntimeContext.hpp"
#include "Environment.hpp"
#include "core/IO.hpp"
#include "process/Launcher.hpp"

namespace havel {

RuntimeContext createPureContext(Environment* env) {
    RuntimeContext ctx;
    ctx.env = env;
    ctx.io = nullptr;
    ctx.executeShell = [](const std::string&) -> ShellResult {
        return ShellResult{"", "", -1, false, ""};
    };
    ctx.reportError = [](const std::string& msg) {
        havel::error(msg);
    };
    ctx.reportWarning = [](const std::string& msg) {
        havel::warn(msg);
    };
    ctx.reportInfo = [](const std::string& msg) {
        havel::info(msg);
    };
    return ctx;
}

RuntimeContext createFullContext(Environment* env, IO* io) {
    RuntimeContext ctx;
    ctx.env = env;
    ctx.io = io;
    
    if (io) {
        ctx.executeShell = [](const std::string& cmd) -> ShellResult {
            // Use Launcher for shell execution
            auto result = Launcher::runShell(cmd);
            return ShellResult{
                result.stdout,
                result.stderr,
                result.exitCode,
                result.success,
                ""  // error field (not used by Launcher)
            };
        };
    } else {
        ctx.executeShell = [](const std::string&) -> ShellResult {
            return ShellResult{"", "", -1, false, ""};
        };
    }
    
    ctx.reportError = [](const std::string& msg) {
        havel::error(msg);
    };
    ctx.reportWarning = [](const std::string& msg) {
        havel::warn(msg);
    };
    ctx.reportInfo = [](const std::string& msg) {
        havel::info(msg);
    };
    
    return ctx;
}

} // namespace havel
