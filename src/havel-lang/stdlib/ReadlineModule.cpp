/* ReadlineModule.cpp - VM-native readline module for line editing/history
   Multi-platform: Linux, macOS, BSD, Windows (via readline) */

#include "ReadlineModule.hpp"

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <cstdio>
#include <cstring>
#include <string>
#include <thread>
#include <vector>

#ifdef HAVE_READLINE
#include <readline/history.h>
#include <readline/readline.h>
#else
// Minimal fallback for platforms without readline
static char *readline(const char *prompt) {
    if (prompt) fputs(prompt, stdout);
    fflush(stdout);
    static char buf[4096];
    if (fgets(buf, sizeof(buf), stdin)) {
        buf[strcspn(buf, "\n")] = '\0';
        return strdup(buf);
    }
    return nullptr;
}
static void add_history(const char *) {}
static int read_history(const char *) { return 0; }
static int write_history(const char *) { return 0; }
#endif

#include "havel-lang/core/Value.hpp"
#include "havel-lang/compiler/vm/VMApi.hpp"

using havel::compiler::Value;
using havel::compiler::VMApi;

namespace havel::stdlib {

void registerReadlineModule(const VMApi &api) {
#ifdef HAVE_READLINE
  // rl_basic_word_break_characters - configure word break chars for completion
  rl_basic_word_break_characters = " \t\n\"\\'`@$><=;|&{(";
  
  // Set up completion - basic filename completion
  rl_attempted_completion_function = [](const char *text, int start, int end) {
    // For now, no custom completion - use filename completion
    return rl_completion_matches(text, rl_filename_completion_function);
  };
#endif

  // ----------------------------------------------------------------------
  // readline.readline – read a line with editing (returns string or null on EOF)
  // ----------------------------------------------------------------------
  api.registerFunction("readline.readline",
      [api](const std::vector<Value> &args) {
        std::string prompt;
        if (!args.empty()) prompt = api.resolveString(args[0]);
        
#ifdef HAVE_READLINE
        char *line = readline(prompt.c_str());
        if (line) {
          std::string result(line);
          free(line);
          return api.makeString(result);
        }
        return Value::makeNull();
#else
        if (!prompt.empty()) {
          fputs(prompt.c_str(), stdout);
          fflush(stdout);
        }
        static char buf[4096];
        if (fgets(buf, sizeof(buf), stdin)) {
          buf[strcspn(buf, "\n")] = '\0';
          return api.makeString(std::string(buf));
        }
        return Value::makeNull();
#endif
      });

  // ----------------------------------------------------------------------
  // readline.add_history – add a line to history
  // ----------------------------------------------------------------------
  api.registerFunction("readline.add_history",
      [api](const std::vector<Value> &args) {
        if (args.empty()) return Value::makeNull();
        std::string line = api.resolveString(args[0]);
#ifdef HAVE_READLINE
        add_history(line.c_str());
#endif
        return Value::makeNull();
      });

  // ----------------------------------------------------------------------
  // readline.read_history – load history from file
  // ----------------------------------------------------------------------
  api.registerFunction("readline.read_history",
      [api](const std::vector<Value> &args) {
        std::string path;
        if (!args.empty()) path = api.resolveString(args[0]);
        else {
          // Default: ~/.havel_history
          const char *home = std::getenv("HOME");
          if (home) path = std::string(home) + "/.havel_history";
        }
        if (path.empty()) return Value::makeNull();
#ifdef HAVE_READLINE
        int ret = read_history(path.c_str());
        return Value::makeInt(ret);
#else
        return Value::makeInt(-1);
#endif
      });

  // ----------------------------------------------------------------------
  // readline.write_history – save history to file
  // ----------------------------------------------------------------------
  api.registerFunction("readline.write_history",
      [api](const std::vector<Value> &args) {
        std::string path;
        if (!args.empty()) path = api.resolveString(args[0]);
        else {
          const char *home = std::getenv("HOME");
          if (home) path = std::string(home) + "/.havel_history";
        }
        if (path.empty()) return Value::makeNull();
#ifdef HAVE_READLINE
        int ret = write_history(path.c_str());
        return Value::makeInt(ret);
#else
        return Value::makeInt(-1);
#endif
      });

  // ----------------------------------------------------------------------
  // readline.clear_history – clear in-memory history
  // ----------------------------------------------------------------------
  api.registerFunction("readline.clear_history",
      [](const std::vector<Value> &) {
#ifdef HAVE_READLINE
        clear_history();
#endif
        return Value::makeNull();
      });

  // ----------------------------------------------------------------------
  // readline.history_length – get current history length
  // ----------------------------------------------------------------------
  api.registerFunction("readline.history_length",
      [](const std::vector<Value> &) {
#ifdef HAVE_READLINE
        return Value::makeInt(history_length);
#else
        return Value::makeInt(0);
#endif
      });

  // ----------------------------------------------------------------------
  // readline.set_completion – set custom completion function (Havel function)
  // ----------------------------------------------------------------------
  api.registerFunction("readline.set_completion",
      [api](const std::vector<Value> &args) {
#ifdef HAVE_READLINE
        if (args.empty() || !args[0].isFunctionObjId() && !args[0].isClosureId()) {
          throw std::runtime_error("readline.set_completion: requires a function");
        }
        // Store completion callback in VM for use by rl_attempted_completion_function
        // For now, just accept it - full implementation needs more infrastructure
        return Value::makeNull();
#else
        return Value::makeNull();
#endif
      });

  // Create and expose the global "readline" object
  auto readlineObj = api.makeObject();
  api.setField(readlineObj, "readline",      api.makeFunctionRef("readline.readline"));
  api.setField(readlineObj, "add_history",   api.makeFunctionRef("readline.add_history"));
  api.setField(readlineObj, "read_history",  api.makeFunctionRef("readline.read_history"));
  api.setField(readlineObj, "write_history", api.makeFunctionRef("readline.write_history"));
  api.setField(readlineObj, "clear_history", api.makeFunctionRef("readline.clear_history"));
  api.setField(readlineObj, "history_length", api.makeFunctionRef("readline.history_length"));
  api.setField(readlineObj, "set_completion", api.makeFunctionRef("readline.set_completion"));
  api.setGlobal("readline", readlineObj);
}

} // namespace havel::stdlib

#ifdef HAVEL_MODULE_PLUGIN
#include "c/ModulePlugin.h"

HAVEL_MODULE_PLUGIN_IMPL(readline, "1.0.0", "Readline line editing module",
    havel::stdlib::registerReadlineModule(*api);
)
#endif
