/* ProcessModule.cpp - VM-native stdlib module */
#include "ProcessModule.hpp"
#include <chrono>
#include <thread>

using havel::compiler::BytecodeValue;
using havel::compiler::VMApi;

namespace havel::stdlib {

// Register process module with VMApi (stable API layer)
void registerProcessModule(VMApi &api) {
  // execute(command) - Execute system command and return result
  api.registerFunction(
      "execute", [&api](const std::vector<BytecodeValue> &args) {
        if (args.empty())
          throw std::runtime_error("execute() requires a command");

        if (!std::holds_alternative<std::string>(args[0]))
          throw std::runtime_error("execute() requires a string command");

        const auto &command = std::get<std::string>(args[0]);

        // Execute command and capture output
        std::ostringstream output;
        std::ostringstream error_output;

        FILE *pipe = popen(command.c_str(), "r");
        if (!pipe)
          throw std::runtime_error("Failed to execute command: " + command);

        char buffer[128];
        while (fgets(buffer, sizeof(buffer), pipe) != nullptr) {
          output << buffer;
        }

        int exit_code = pclose(pipe);

        // Create result object
        auto result_obj = api.makeObject();
        api.setField(result_obj, "output", BytecodeValue(output.str()));
        api.setField(result_obj, "exit_code",
                     BytecodeValue(static_cast<int64_t>(exit_code)));

        return BytecodeValue(result_obj);
      });

  // getpid() - Get current process ID
  api.registerFunction("getpid",
                       [&api](const std::vector<BytecodeValue> &args) {
                         (void)args;
                         return BytecodeValue(static_cast<int64_t>(::getpid()));
                       });

  // getppid() - Get parent process ID
  api.registerFunction(
      "getppid", [&api](const std::vector<BytecodeValue> &args) {
        (void)args;
        return BytecodeValue(static_cast<int64_t>(::getppid()));
      });

  // sleep(seconds) - Sleep for specified seconds
  api.registerFunction("sleep", [&api](const std::vector<BytecodeValue> &args) {
    if (args.empty())
      throw std::runtime_error("sleep() requires seconds");

    double seconds = 0.0;
    if (std::holds_alternative<int64_t>(args[0]))
      seconds = static_cast<double>(std::get<int64_t>(args[0]));
    else if (std::holds_alternative<double>(args[0]))
      seconds = std::get<double>(args[0]);
    else
      throw std::runtime_error("sleep() requires a number");

    if (seconds < 0.0)
      throw std::runtime_error("sleep() seconds must be non-negative");

    std::this_thread::sleep_for(
        std::chrono::milliseconds(static_cast<int64_t>(seconds * 1000.0)));

    return BytecodeValue(true);
  });

  // env(name) - Get environment variable
  api.registerFunction("env", [&api](const std::vector<BytecodeValue> &args) {
    if (args.empty())
      throw std::runtime_error("env() requires variable name");

    if (!std::holds_alternative<std::string>(args[0]))
      throw std::runtime_error("env() requires a string variable name");

    const auto &name = std::get<std::string>(args[0]);
    const char *value = std::getenv(name.c_str());

    if (value == nullptr)
      return BytecodeValue(std::string(""));

    return BytecodeValue(std::string(value));
  });

  // setenv(name, value) - Set environment variable
  api.registerFunction(
      "setenv", [&api](const std::vector<BytecodeValue> &args) {
        if (args.size() < 2)
          throw std::runtime_error("setenv() requires name and value");

        if (!std::holds_alternative<std::string>(args[0]) ||
            !std::holds_alternative<std::string>(args[1]))
          throw std::runtime_error("setenv() requires string arguments");

        const auto &name = std::get<std::string>(args[0]);
        const auto &value = std::get<std::string>(args[1]);

        int result = ::setenv(name.c_str(), value.c_str(), 1); // 1 = overwrite

        return BytecodeValue(result == 0);
      });

  // exit(code) - Exit current process
  api.registerFunction("exit", [&api](const std::vector<BytecodeValue> &args) {
    int exit_code = 0;

    if (!args.empty()) {
      if (std::holds_alternative<int64_t>(args[0]))
        exit_code = static_cast<int>(std::get<int64_t>(args[0]));
      else
        throw std::runtime_error("exit() requires an integer exit code");
    }

    std::exit(exit_code);

    // This should never be reached
    return BytecodeValue(false);
  });

  // Register process object
  auto processObj = api.makeObject();
  api.setField(processObj, "execute", api.makeFunctionRef("execute"));
  api.setField(processObj, "getpid", api.makeFunctionRef("getpid"));
  api.setField(processObj, "getppid", api.makeFunctionRef("getppid"));
  api.setField(processObj, "sleep", api.makeFunctionRef("sleep"));
  api.setField(processObj, "env", api.makeFunctionRef("env"));
  api.setField(processObj, "setenv", api.makeFunctionRef("setenv"));
  api.setField(processObj, "exit", api.makeFunctionRef("exit"));
  api.setGlobal("Process", processObj);
}

} // namespace havel::stdlib
