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
#include <set>

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

#ifdef HAVE_READLINE
// Static completion state
static std::set<std::string> g_completion_words;
static bool g_completion_initialized = false;

// readline completion generator - returns next match or nullptr
static char *completion_generator(const char *text, int state) {
    static std::vector<std::string>::const_iterator it;
    static std::vector<std::string> matches;
    
    if (state == 0) {
        matches.clear();
        for (const auto &word : g_completion_words) {
            if (word.rfind(text, 0) == 0) { // word starts with text
                matches.push_back(word);
            }
        }
        it = matches.begin();
    }
    
    if (it != matches.end()) {
        char *result = strdup(it->c_str());
        ++it;
        return result;
    }
    return nullptr;
}
#endif

// ----------------------------------------------------------------------
// readline.readline – read a line with editing (returns string or null on EOF)
// ----------------------------------------------------------------------
static void register_readline_functions(const VMApi &api) {
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
    // readline.add_completion_word – add a word to completion dictionary
    // ----------------------------------------------------------------------
    api.registerFunction("readline.add_completion_word",
        [api](const std::vector<Value> &args) {
#ifdef HAVE_READLINE
            if (args.empty()) return Value::makeNull();
            std::string word = api.resolveString(args[0]);
            g_completion_words.insert(word);
            // Initialize readline completion if not done
            if (!g_completion_initialized) {
                rl_attempted_completion_function = [](const char *text, int start, int end) {
                    return rl_completion_matches(text, completion_generator);
                };
                rl_completion_append_character = '\0';
                rl_basic_word_break_characters = " \t\n\"\\'`@$><=;|&{(";
                g_completion_initialized = true;
            }
#endif
            return Value::makeNull();
        });

    // ----------------------------------------------------------------------
    // readline.clear_completion_words – clear completion dictionary
    // ----------------------------------------------------------------------
    api.registerFunction("readline.clear_completion_words",
        [](const std::vector<Value> &) {
#ifdef HAVE_READLINE
            g_completion_words.clear();
            g_completion_initialized = false;
            rl_attempted_completion_function = nullptr;
#endif
            return Value::makeNull();
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

    // Create and expose the global "readline" object
    auto readlineObj = api.makeObject();
    api.setField(readlineObj, "readline",            api.makeFunctionRef("readline.readline"));
    api.setField(readlineObj, "add_history",         api.makeFunctionRef("readline.add_history"));
    api.setField(readlineObj, "read_history",        api.makeFunctionRef("readline.read_history"));
    api.setField(readlineObj, "write_history",       api.makeFunctionRef("readline.write_history"));
    api.setField(readlineObj, "clear_history",       api.makeFunctionRef("readline.clear_history"));
    api.setField(readlineObj, "history_length",      api.makeFunctionRef("readline.history_length"));
    api.setField(readlineObj, "add_completion_word", api.makeFunctionRef("readline.add_completion_word"));
    api.setGlobal("readline", readlineObj);
}

void registerReadlineModule(const VMApi &api) {
    register_readline_functions(api);
}

} // namespace havel::stdlib

#ifdef HAVEL_MODULE_PLUGIN
#include "c/ModulePlugin.h"

HAVEL_MODULE_PLUGIN_IMPL(readline, "1.0.0", "Readline line editing module",
    havel::stdlib::registerReadlineModule(*api);
)
#endif
