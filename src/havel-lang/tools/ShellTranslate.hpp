/*
 * ShellTranslate.hpp
 *
 * Shell-mode translation for the REPL: turns bash-shaped input into Havel
 * source. One grammar stays (Havel); only the command layer is shell-shaped:
 *
 *   ls -la | grep txt            -> process.pipe([["ls", "-la"], ["grep", "txt"]])
 *   x = $(ls)                    -> x = process.pipe([["ls"]]).stdout.trim()
 *   files = *.log                -> files = fs.glob("*.log")
 *   export FOO=bar               -> shell.env("FOO", "bar")
 *   cd /tmp                      -> shell.cd("/tmp")
 *   echo $FOO                    -> print((sys.env("FOO") || ""))
 *   if [ -f /tmp/x ] { echo yes }-> if fs.isFile("/tmp/x") { print("yes") }
 *
 * Control flow keeps Havel's braces (no then/fi/done). Unsupported bash
 * syntax is rejected with a suggestion instead of guessing.
 */
#pragma once

#include <string>
#include <unordered_set>

namespace havel::repl {

// Result of translating shell-shaped input into Havel.
struct ShellTranslation {
  bool ok = true;
  std::string source;  // Havel source when ok
  std::string error;   // user-facing message when !ok
  // True when the last statement is a shell side-effect (cd/export, a
  // redirect, exit): the REPL must not print its return value, matching a
  // shell where those commands are silent.
  bool silent = false;
};

// Translate one shell-mode line into Havel.
// `knownGlobals` (may be nullptr): names that already exist as Havel
// variables in the session. `$name` naming a known global (or a local of the
// enclosing block, tracked internally) is emitted as that variable;
// otherwise it reads the process environment at run time.
ShellTranslation translateShellLine(const std::string& line,
                                    const std::unordered_set<std::string>* knownGlobals);

// Translate a whole accumulated shell-mode block (multi-line input).
// Control-flow lines keep Havel's grammar; statements inside their braces and
// shell-shaped statements on their own lines are translated per line.
ShellTranslation translateShellBlock(const std::string& block,
                                     const std::unordered_set<std::string>* knownGlobals);

}  // namespace havel::repl
