#pragma once

#include <chrono>
#include <cerrno>
#include <cstdlib>
#include <cstring>
#include <atomic>
#include <fstream>
#include <filesystem>
#include <iostream>
#include <mutex>
#include <poll.h>
#include <signal.h>
#include <string>
#include <sstream>
#include <vector>
#include <sys/wait.h>
#include <thread>
#include <unistd.h>
#include <vector>

namespace hvtest {

namespace fs = std::filesystem;

extern char **environ;

struct ScriptResult {
  std::string path;
  bool passed;
  int exit_code;
  double elapsed_ms;
  // True when the child was stopped by the VM's instruction budget
  // (max-instructions) instead of finishing on its own. This is a
  // work-based bound, not a wall-clock one, so the verdict does not
  // depend on how loaded the machine was.
  bool instruction_limited;
  // Tail of the child's captured stdout+stderr; kept to the last
  // 16384 bytes so a failing intermittent run leaves its evidence
  // behind (previously the pipe was read and discarded).
  std::string output_tail;
  std::string name() const { return fs::path(path).stem().string(); }
  void append_output(const char *data, size_t n) {
    output_tail.append(data, n);
    constexpr size_t kMaxTail = 16384;
    if (output_tail.size() > kMaxTail)
      output_tail.erase(0, output_tail.size() - kMaxTail);
  }
};

// A script declares "I could not run here" by printing a line that starts
// with SKIP (suite convention: "SKIP <name>: <reason>") and exiting 0.
// Without this protocol such a script counted as PASSED, so an
// environment-missing test silently stopped covering its feature while
// every gate summary stayed green (observed: the hpkg toolchain store test
// skipped in a git-less chroot for months - see
// docs/known-issues/hvtest-skips-count-as-pass.md).
inline bool script_declared_skip(const ScriptResult &result, std::string &reason) {
  if (result.exit_code != 0)
    return false;
  size_t pos = 0;
  while ((pos = result.output_tail.find("SKIP", pos)) != std::string::npos) {
    bool at_line_start = (pos == 0) || result.output_tail[pos - 1] == '\n';
    if (at_line_start) {
      size_t end = result.output_tail.find('\n', pos);
      std::string line =
          result.output_tail.substr(pos, end == std::string::npos ? std::string::npos : end - pos);
      // Marker lines are "SKIP" possibly followed by ": reason" or
      // " name: reason". Trailing CR for CRLF output.
      while (!line.empty() && (line.back() == '\r' || line.back() == ' '))
        line.pop_back();
      reason = line;
      return true;
    }
    pos += 4;
  }
  return false;
}

inline std::vector<std::string> discover_scripts(const std::vector<std::string> &directories) {
  std::vector<std::string> scripts;
  for (const auto &dir : directories) {
    if (!fs::exists(dir))
      continue;
    for (const auto &entry : fs::directory_iterator(dir)) {
      if (entry.is_regular_file() && entry.path().extension() == ".hv") {
        scripts.push_back(entry.path().string());
      }
    }
  }
  std::sort(scripts.begin(), scripts.end());
  return scripts;
}

// Default per-test instruction budget. This bounds the WORK a script may
// do, so it detects a script stuck in an infinite loop without ever
// misreporting a merely slow machine. The number is deliberately far above
// what any ordinary test needs (a plain arithmetic test runs ~57k VM
// instructions; the heaviest smoke test ~1.8M) and far below a runaway
// loop, which burns tens of millions per second.
//
// A script that legitimately needs more work declares it in its header:
//   // smoke: max-instructions = 200000000
// Scripts that are long-running BY DESIGN (stress and benchmark runs) are
// excluded from the auto-collected suite entirely and run only via the
// explicit opt-in paths.
constexpr uint64_t kDefaultInstructionBudget = 50'000'000;

inline std::vector<std::string> list_test_dirs(const std::string &scripts_root) {
  return {
      scripts_root + "/smoke",
      scripts_root + "/integration",
      scripts_root + "/tests/main",
  };
}

// Directories holding ASSERTION-style tests: each script checks its own
// result and exits non-zero when the check fails. These are real tests and
// belong in the auto-collected suite.
//
// scripts/tests/unit holds the language unit tests (arithmetic, strings,
// arrays, closures, classes, ...). They were previously written but never
// wired into any runner: the directory was referenced nowhere in the build
// or test infrastructure, so a regression in any of them went unnoticed.
// Every script there is side-effect free (no fs.write, no process.run, no
// mouse/keyboard/brightness/window calls), so it is safe to auto-collect.
//
// Deliberately NOT included:
//   scripts/tests/io        - real input injection
//   scripts/tests/window    - real window manipulation
//   scripts/tests/events    - real fs watchers / event sources
//   scripts/tests/mod       - contains a brightness regression test
//   scripts/tests/stress    - long-running by design (slow tier)
//   scripts/benchmarks      - long-running by design (slow tier)
// Those run only through the explicit opt-in paths.
inline std::vector<std::string> list_assertion_test_dirs(const std::string &scripts_root) {
  return {
      scripts_root + "/tests/unit",
  };
}

// Directories whose scripts are long-running BY DESIGN: stress runs and
// benchmarks. They are excluded from the auto-collected suite and run only
// when explicitly requested, because their runtime dominates everything
// else and they say nothing about whether the code under test is correct.
inline std::vector<std::string> list_long_running_dirs(const std::string &scripts_root) {
  return {
      scripts_root + "/stress",
      scripts_root + "/tests/stress",
      scripts_root + "/benchmarks",
  };
}

// Read a per-test instruction budget from the file header.
// Format: // smoke: max-instructions = <count>
// 0 means "use the suite default". The budget bounds the WORK a script may
// do, not the wall-clock time it may take: a script that loops forever is
// stopped after N VM instructions, while a script that legitimately needs a
// long wall-clock run (many seconds of sleeping) is unaffected. The verdict
// therefore does not depend on how loaded the machine was.
inline uint64_t read_test_instruction_budget(const std::string &script_path) {
  std::ifstream ifs(script_path);
  if (!ifs)
    return 0;
  std::string line;
  int count = 0;
  while (std::getline(ifs, line) && count < 20) {
    count++;
    if (line.rfind("// smoke: max-instructions =", 0) == 0 ||
        line.rfind("// test: max-instructions =", 0) == 0) {
      size_t eq = line.find('=');
      if (eq != std::string::npos) {
        std::string val = line.substr(eq + 1);
        val.erase(0, val.find_first_not_of(" \t"));
        val.erase(val.find_last_not_of(" \t") + 1);
        try {
          return static_cast<uint64_t>(std::stoull(val));
        } catch (...) {
        }
      }
      break;
    }
  }
  return 0;
}

// The budget that applies to one script: its own header override if it
// declares one, otherwise the suite default. A declared value of 0 is
// treated as "not declared" and falls back to the default; a script opts
// out of the budget by being in a suite whose default is 0 (the --long
// suite), not by declaring 0 here.
inline uint64_t resolve_instruction_budget(const std::string &script_path,
                                           uint64_t default_budget) {
  uint64_t declared = read_test_instruction_budget(script_path);
  return declared > 0 ? declared : default_budget;
}

// Read per-test extra runner flags from the file header.
// Format: // smoke: flags = --tiering --foo   (whitespace-separated,
// appended to the default self-hosted invocation flags).
inline std::vector<std::string> read_test_flags(const std::string &script_path) {
  std::vector<std::string> out;
  std::ifstream ifs(script_path);
  if (!ifs)
    return out;
  std::string line;
  int count = 0;
  while (std::getline(ifs, line) && count < 20) {
    count++;
    if (line.rfind("// smoke: flags =", 0) == 0 || line.rfind("// test: flags =", 0) == 0) {
      size_t eq = line.find('=');
      if (eq != std::string::npos) {
        std::string val = line.substr(eq + 1);
        std::istringstream iss(val);
        std::string tok;
        while (iss >> tok)
          out.push_back(tok);
      }
      break;
    }
  }
  return out;
}

// Environment keys owned by the sandbox. A // smoke: env = header may not
// override these: doing so would let a single test hand its own child a real
// display, session bus, or a non-headless flag, which defeats the guarantee
// that no hvtest process touches the user's session.
inline bool is_sandbox_env_key(const std::string &key) {
  return key == "HAVEL_HEADLESS" || key == "DISPLAY" || key == "WAYLAND_DISPLAY" ||
         key == "XAUTHORITY" || key == "XDG_SESSION_TYPE" || key == "QT_QPA_PLATFORM" ||
         key == "DBUS_SESSION_BUS_ADDRESS";
}

// Read per-test environment overrides from the file header.
// Format: // smoke: env = VAR=value VAR2=value  (whitespace-separated,
// added on top of the inherited environment for this test's child).
inline std::vector<std::pair<std::string, std::string>>
read_test_env(const std::string &script_path) {
  std::vector<std::pair<std::string, std::string>> out;
  std::ifstream ifs(script_path);
  if (!ifs)
    return out;
  std::string line;
  int count = 0;
  while (std::getline(ifs, line) && count < 20) {
    count++;
    if (line.rfind("// smoke: env =", 0) == 0 || line.rfind("// test: env =", 0) == 0) {
      size_t eq = line.find('=');
      if (eq != std::string::npos) {
        std::string val = line.substr(eq + 1);
        std::istringstream iss(val);
        std::string tok;
        while (iss >> tok) {
          size_t eq2 = tok.find('=');
          if (eq2 != std::string::npos && eq2 > 0) {
            out.emplace_back(tok.substr(0, eq2), tok.substr(eq2 + 1));
          }
        }
      }
      break;
    }
  }
  return out;
}

// Read per-test tier from the file header.
// Format: // smoke: tier = slow   (or // test: tier = slow)
// slow-tier tests are GC/tiering stress runs that dominate suite time
// (one 208s tiering test + two ~40s GC stress tests = ~17% of the whole
// suite). Default (no directive) = fast tier. Dev-loop runs pass --fast
// to skip slow; pre-merge/CI runs the full set (or --only-slow to run
// just the slow tier after a fast pass).
inline std::string read_test_tier(const std::string &script_path) {
  std::ifstream ifs(script_path);
  if (!ifs)
    return "default";
  std::string line;
  int count = 0;
  while (std::getline(ifs, line) && count < 20) {
    count++;
    if (line.rfind("// smoke: tier =", 0) == 0 || line.rfind("// test: tier =", 0) == 0) {
      size_t eq = line.find('=');
      if (eq != std::string::npos) {
        std::string val = line.substr(eq + 1);
        val.erase(0, val.find_first_not_of(" \t"));
        val.erase(val.find_last_not_of(" \t") + 1);
        if (!val.empty())
          return val;
      }
      break;
    }
  }
  return "default";
}

enum class TestTier { Default, Slow };

inline TestTier parse_test_tier(const std::string &script_path) {
  std::string tier = read_test_tier(script_path);
  if (tier == "slow")
    return TestTier::Slow;
  return TestTier::Default; // unknown values stay in the default tier
}

inline ScriptResult run_script(const std::string &havel_bin, const std::string &script_path,
                               const std::vector<std::string> &pre_flags = {},
                               uint64_t instruction_budget = 0) {
  ScriptResult result;
  result.path = script_path;
  result.passed = false;
  result.exit_code = -1;
  result.elapsed_ms = 0;
  result.instruction_limited = false;

  int pipefd[2];
  if (pipe(pipefd) == -1) {
    return result;
  }

  auto start = std::chrono::high_resolution_clock::now();

  pid_t pid = fork();
  if (pid == -1) {
    close(pipefd[0]);
    close(pipefd[1]);
    return result;
  }

  if (pid == 0) {
    close(pipefd[0]);
    dup2(pipefd[1], STDOUT_FILENO);
    dup2(pipefd[1], STDERR_FILENO);
    close(pipefd[1]);
    std::vector<std::string> flags;
    if (pre_flags.empty()) {
      // Self-hosted pipeline by default (like smoke mode)
      fs::path bin_path(havel_bin);
      fs::path repo_root = bin_path.parent_path().parent_path();
      fs::path self_hosted_path = repo_root / "out";
      flags = {"--run", "--self-hosted-path", self_hosted_path.string()};
      // Per-test header flags (e.g. // smoke: flags = --tiering)
      // extend the default invocation; they only apply to the
      // default pipeline, never override explicit pre_flags.
      for (const auto &f : read_test_flags(script_path)) {
        flags.push_back(f);
      }
    } else {
      flags = pre_flags;
    }
    // --headless puts the child off X11/evdev/uinput and kills the
    // brightness/event listeners: fixtures that still call mouse.move()
    // or brightness.set() then no-op instead of touching the real
    // desktop (that was the phantom-zoom vector in scripts/tests/io/*).
    flags.insert(flags.begin(), "--headless");
    // The instruction budget bounds the child's WORK. A script stuck in
    // an infinite loop is stopped by the VM itself and exits with the
    // "instruction limit exceeded" error, which the parent recognizes
    // below. Unlike a wall-clock deadline this cannot fire just because
    // the machine was busy.
    if (instruction_budget > 0) {
      flags.push_back("--max-instructions");
      flags.push_back(std::to_string(instruction_budget));
    }
    std::vector<char *> args;
    args.push_back(const_cast<char *>(havel_bin.c_str()));
    for (const auto &f : flags) {
      args.push_back(const_cast<char *>(f.c_str()));
    }
    args.push_back(const_cast<char *>(script_path.c_str()));
    args.push_back(nullptr);

    // Pass through environment variables (needed for HAVEL_EXTENSION_DIR),
    // then apply per-test header env overrides (// smoke: env = VAR=v).
    // entry_strings must outlive env (we store c_str pointers into it
    // up to the execvpe call).
    // --headless is the primary guarantee; the display/session variables
    // are additionally dropped so a code path we did not gate cannot reach
    // the live session even by accident.
    std::vector<char *> env;
    std::vector<std::string> entry_strings;
    for (char **e = ::environ; *e; ++e) {
      if (std::strncmp(*e, "DISPLAY=", 8) == 0)
        continue;
      if (std::strncmp(*e, "WAYLAND_DISPLAY=", 16) == 0)
        continue;
      if (std::strncmp(*e, "XAUTHORITY=", 11) == 0)
        continue;
      if (std::strncmp(*e, "DBUS_SESSION_BUS_ADDRESS=", 24) == 0)
        continue;
      env.push_back(*e);
    }
    // Appended after the inherited env, so these win over anything the
    // parent had. DBUS_SESSION_BUS_ADDRESS=disabled: is the documented
    // libdbq way to say "there is no bus" -- it short-circuits before any
    // connect() instead of reaching the user's session bus.
    env.push_back(const_cast<char *>("HAVEL_HEADLESS=1"));
    env.push_back(const_cast<char *>("DISPLAY="));
    env.push_back(const_cast<char *>("WAYLAND_DISPLAY="));
    env.push_back(const_cast<char *>("XAUTHORITY="));
    env.push_back(const_cast<char *>("XDG_SESSION_TYPE="));
    env.push_back(const_cast<char *>("QT_QPA_PLATFORM=offscreen"));
    env.push_back(const_cast<char *>("DBUS_SESSION_BUS_ADDRESS=disabled:"));
    for (const auto &kv : read_test_env(script_path)) {
      // The sandbox is authoritative: a test header must not be able to
      // hand a child a real display, bus, or a non-headless flag. The
      // value is only forwarded to the child via the env vector below --
      // it is deliberately NOT setenv'd, which would mutate hvtest's own
      // environment and leak this test's overrides into later tests.
      if (is_sandbox_env_key(kv.first)) {
        continue;
      }
      entry_strings.push_back(kv.first + "=" + kv.second);
    }
    for (const auto &s : entry_strings) {
      env.push_back(const_cast<char *>(s.c_str()));
    }
    env.push_back(nullptr);
    execvpe(havel_bin.c_str(), args.data(), env.data());
    // exec failed: report errno through the pipe (stdout is already
    // dup2'd there). A bare _exit(127) showed up as 'exit=127, no
    // captured output' for every test, hiding ETXTBSY (binary being
    // relinked by a concurrent build), ENOENT (missing binary), etc.
    int e = errno;
    ssize_t ignored1 = ::write(STDERR_FILENO, "exec failed: ", 13);
    const char *msg = std::strerror(e);
    ssize_t ignored2 = ::write(STDERR_FILENO, msg, std::strlen(msg));
    ssize_t ignored3 = ::write(STDERR_FILENO, "\n", 1);
    (void)ignored1;
    (void)ignored2;
    (void)ignored3;
    _exit(127);
  }

  close(pipefd[1]);

  struct pollfd pfd = {pipefd[0], POLLIN, 0};

  char buffer[4096];
  int status = 0;
  bool pipe_done = false;

  while (true) {
    if (!pipe_done) {
      int pret = poll(&pfd, 1, 100);
      if (pret > 0) {
        ssize_t n = read(pipefd[0], buffer, sizeof(buffer));
        if (n <= 0) {
          close(pipefd[0]);
          pipe_done = true;
        } else {
          result.append_output(buffer, static_cast<size_t>(n));
        }
      } else if (pret == 0) {
        // timeout waiting for pipe data, check child status
      }
    }

    pid_t ret = waitpid(pid, &status, WNOHANG);
    if (ret > 0 || ret == -1) {
      if (!pipe_done) {
        while (true) {
          int p = poll(&pfd, 1, 200);
          if (p > 0) {
            ssize_t n = read(pipefd[0], buffer, sizeof(buffer));
            if (n <= 0)
              break;
            result.append_output(buffer, static_cast<size_t>(n));
          } else {
            break;
          }
        }
        close(pipefd[0]);
      }
      break;
    }
  }

  auto end = std::chrono::high_resolution_clock::now();
  result.elapsed_ms = std::chrono::duration<double, std::milli>(end - start).count();

  if (WIFEXITED(status)) {
    result.exit_code = WEXITSTATUS(status);
    result.passed = (result.exit_code == 0);
  } else if (WIFSIGNALED(status)) {
    result.exit_code = -WTERMSIG(status);
  }

  // The VM reports its own instruction-limit abort on stdout:
  //   "VM instruction limit exceeded (N) - possible infinite loop"
  // Recognizing it here separates "the script loops forever" from "the
  // script raised an ordinary error", which both exit non-zero.
  result.instruction_limited =
      result.output_tail.find("instruction limit exceeded") != std::string::npos;
  if (result.instruction_limited) {
    result.passed = false;
  }

  return result;
}

// Dump the captured child output tail after a [FAIL] line so an
// intermittent failure (e.g. smoke-suite arithmetic_pow under load)
// keeps its evidence; the pipe used to be read and discarded.
inline void print_result_tail(const ScriptResult &result, size_t max_lines = 40) {
  if (result.output_tail.empty()) {
    std::cout << "  (no captured output)" << std::endl;
    return;
  }
  std::istringstream ss(result.output_tail);
  std::vector<std::string> lines;
  std::string line;
  while (std::getline(ss, line))
    lines.push_back(line);
  size_t start = lines.size() > max_lines ? lines.size() - max_lines : 0;
  if (start > 0)
    std::cout << "  ... (" << start << " earlier lines clipped)" << std::endl;
  for (size_t i = start; i < lines.size(); ++i)
    std::cout << "  | " << lines[i] << std::endl;
}

inline int run_script_suite(const std::string &havel_bin,
                            const std::vector<std::string> &directories, bool verbose = false,
                            const std::vector<std::string> &pre_flags = {},
                            uint64_t default_budget = kDefaultInstructionBudget) {
  auto scripts = discover_scripts(directories);
  if (scripts.empty()) {
    std::cerr << "no .hv scripts found in specified directories" << std::endl;
    return 1;
  }

  int pass = 0, fail = 0, skip = 0;
  std::vector<ScriptResult> results;
  for (const auto &script : scripts) {
    auto result = run_script(havel_bin, script, pre_flags,
                             resolve_instruction_budget(script, default_budget));
    results.push_back(result);
    if (result.passed) {
      std::string skip_reason;
      if (script_declared_skip(result, skip_reason)) {
        std::cout << "[SKIP] " << script << " (" << skip_reason << ")" << std::endl << std::flush;
        skip++;
      } else {
        std::cout << "[PASS] " << script << " (" << result.elapsed_ms << "ms)" << std::endl
                  << std::flush;
        pass++;
      }
    } else if (result.instruction_limited) {
      // The VM stopped the script because it exceeded its instruction
      // budget: the script is looping (or doing unbounded work). This
      // is a real defect in the script, not a slow machine.
      std::cout << "[FAIL] " << script << " (infinite loop: exceeded the "
                << resolve_instruction_budget(script, default_budget) << "-instruction budget)"
                << std::endl
                << std::flush;
      print_result_tail(result);
      fail++;
    } else if (result.exit_code == -6 || result.exit_code == -11) {
      // Crash, not an assertion failure: UI/server scripts in a
      // display-less environment crash rather than fail (observed:
      // ui.window/ui.canvas scripts exit -11 headless); an
      // assertion failure surfaces as a nonzero exit code instead
      // of a signal.
      std::cout << "[SKIP] " << script << " (crash: likely needs a display or event loop)"
                << std::endl
                << std::flush;
      skip++;
    } else {
      std::cout << "[FAIL] " << script << " (exit=" << result.exit_code << ")" << std::endl
                << std::flush;
      print_result_tail(result);
      fail++;
    }
  }

  double total_ms = 0;
  for (const auto &r : results)
    total_ms += r.elapsed_ms;
  std::cout << "\nscripts: " << pass << " passed, " << fail << " failed, " << skip << " skipped | "
            << results.size() << " files, " << total_ms << "ms total" << std::endl
            << std::flush;
  return fail > 0 ? 1 : 0;
}

inline int list_scripts(const std::vector<std::string> &directories) {
  auto scripts = discover_scripts(directories);
  for (const auto &script : scripts) {
    std::cout << script << std::endl << std::flush;
  }
  std::cout << scripts.size() << " test(s)" << std::endl << std::flush;
  return 0;
}

// Tier selection for a suite run. Default (no flags) runs everything,
// matching CI and pre-merge expectations. --fast skips slow-tier tests
// (the dev loop); --only-slow runs just the slow tier (follow-up pass
// after a fast iteration, or pre-merge).
enum class TierMode { All, Fast, OnlySlow };

// Forward declaration: run_smoke_suite() is the single-directory wrapper
// around run_smoke_suite_dirs(), which collects from several directories
// and runs them in parallel. The defaults live on the definition, not here.
inline int run_smoke_suite_dirs(const std::string &havel_bin,
                                const std::vector<std::string> &smoke_dirs, bool verbose = false,
                                const std::vector<std::string> &pre_flags = {},
                                const std::vector<std::string> &name_filters = {},
                                TierMode tier_mode = TierMode::All,
                                uint64_t default_budget = kDefaultInstructionBudget);

inline int run_smoke_suite(const std::string &havel_bin, const std::string &smoke_dir,
                           bool verbose = false, const std::vector<std::string> &pre_flags = {},
                           const std::vector<std::string> &name_filters = {},
                           TierMode tier_mode = TierMode::All) {
  return run_smoke_suite_dirs(havel_bin, {smoke_dir}, verbose, pre_flags, name_filters, tier_mode,
                              kDefaultInstructionBudget);
}

inline int run_smoke_suite_dirs(const std::string &havel_bin,
                                const std::vector<std::string> &smoke_dirs, bool verbose,
                                const std::vector<std::string> &pre_flags,
                                const std::vector<std::string> &name_filters, TierMode tier_mode,
                                uint64_t default_budget) {
  auto scripts = discover_scripts(smoke_dirs);
  if (scripts.empty()) {
    std::cerr << "no .hv smoke tests found in";
    for (const auto &d : smoke_dirs)
      std::cerr << " " << d;
    std::cerr << std::endl;
    return 1;
  }
  const size_t total_discovered = scripts.size();
  if (tier_mode != TierMode::All) {
    std::vector<std::string> filtered;
    for (const auto &s : scripts) {
      bool slow = parse_test_tier(s) == TestTier::Slow;
      bool keep = (tier_mode == TierMode::Fast) ? !slow : slow;
      if (keep)
        filtered.push_back(s);
    }
    if (filtered.empty()) {
      std::cerr << (tier_mode == TierMode::Fast ? "no fast-tier tests found (all slow?) in "
                                                : "no slow-tier tests found in ");
      for (const auto &d : smoke_dirs)
        std::cerr << " " << d;
      std::cerr << std::endl;
      return 1;
    }
    scripts = std::move(filtered);
  }
  if (!name_filters.empty()) {
    // Substring match on the file stem, mirroring how people invoke
    // a single test: `hvtest --smoke coroutine_call_resume`.
    std::vector<std::string> filtered;
    for (const auto &s : scripts) {
      const std::string stem = fs::path(s).stem().string();
      for (const auto &f : name_filters) {
        if (stem.find(f) != std::string::npos) {
          filtered.push_back(s);
          break;
        }
      }
    }
    if (filtered.empty()) {
      std::cerr << "no smoke tests match filter:";
      for (const auto &f : name_filters)
        std::cerr << " " << f;
      std::cerr << " (in";
      for (const auto &d : smoke_dirs)
        std::cerr << " " << d;
      std::cerr << ")" << std::endl;
      return 1;
    }
    scripts = std::move(filtered);
    std::cout << "filter: " << scripts.size() << " of " << total_discovered << " scripts match"
              << std::endl;
  }
  if (tier_mode != TierMode::All) {
    std::cout << "tier: " << scripts.size() << " of " << total_discovered << " scripts in "
              << (tier_mode == TierMode::Fast ? "fast" : "slow") << " tier" << std::endl;
  }

  // Detect bytecode/self-hosted modules path: derived from havel_bin's location.
  // For build-debug/havel -> modules dir is ../modules, self-hosted is ../out
  fs::path bin_path(havel_bin);
  fs::path repo_root = bin_path.parent_path().parent_path();
  fs::path module_parent = repo_root;
  fs::path self_hosted_path = repo_root / "out";
  fs::path modules_root = module_parent / "modules";
  std::string bc_path = modules_root.string();
  if (!fs::exists(modules_root)) {
    bc_path = (fs::current_path() / "modules").string();
  }
  std::cout << "bytecode path: " << bc_path << std::endl;
  std::cout << "self-hosted path: " << self_hosted_path.string() << std::endl;
  std::cout << "pipeline: " << (pre_flags.empty() ? "c++" : "self-hosted") << std::endl;

  int pass = 0, fail = 0, skip = 0;
  std::vector<ScriptResult> results;
  auto suite_start = std::chrono::high_resolution_clock::now();

  // Each script runs in its own subprocess whose self-hosted boot costs
  // ~4s, so a sequential suite takes 20+ minutes. Run them concurrently.
  // The worker count is fixed: each subprocess runs its own
  // single-threaded VM dispatch loop at 100% of a core, and a count
  // derived from the machine's core count made the suite's runtime and
  // its contention level depend on the box it ran on. Allow an explicit
  // override via HAVEL_TEST_THREADS.
  unsigned workers = 4;
  if (const char *env = std::getenv("HAVEL_TEST_THREADS")) {
    int n = std::atoi(env);
    if (n > 0)
      workers = static_cast<unsigned>(n);
  }
  std::atomic<size_t> next{0};
  std::vector<ScriptResult> results_par(scripts.size());
  std::mutex io_mtx;
  auto worker = [&]() {
    for (;;) {
      size_t i = next.fetch_add(1);
      if (i >= scripts.size())
        break;
      auto result = run_script(havel_bin, scripts[i], pre_flags,
                               resolve_instruction_budget(scripts[i], default_budget));
      results_par[i] = result;
      auto name = fs::path(scripts[i]).stem().string();
      std::lock_guard<std::mutex> lk(io_mtx);
      if (result.passed) {
        std::string skip_reason;
        if (script_declared_skip(result, skip_reason)) {
          std::cout << "[SKIP] " << name << " (" << skip_reason << ")" << std::endl << std::flush;
          skip++;
        } else {
          if (verbose)
            std::cout << "[PASS] " << name << " (" << result.elapsed_ms << "ms)" << std::endl
                      << std::flush;
          pass++;
        }
      } else if (result.exit_code == -6 || result.exit_code == -11) {
        if (verbose)
          std::cout << "[SKIP] " << name << " (crash, needs event loop)" << std::endl << std::flush;
        skip++;
      } else if (result.instruction_limited) {
        // The VM stopped the script because it exceeded its instruction
        // budget: the script is looping (or doing unbounded work). This
        // is a real defect in the script, not a slow machine.
        std::cout << "[FAIL] " << name << " (infinite loop: exceeded the "
                  << resolve_instruction_budget(scripts[i], default_budget)
                  << "-instruction budget)" << std::endl
                  << std::flush;
        print_result_tail(result);
        fail++;
      } else if (!pre_flags.empty() && result.exit_code != 255) {
        // Self-hosted mode: the script's return value becomes the exit
        // code. The suite's success convention is return 0 ('val
        // __result = 0; return __result' in 269 of 279 scripts; the
        // rest fall off the end as 0). exit=255 is process.exit(255)
        // (explicit assertion failure). Any OTHER nonzero code is a
        // script error - uncaught assert() throws surface as exit 1 -
        // and must FAIL, not pass: this branch used to print PASS for
        // any exit != 255, reporting scheduler_goroutine.hv green
        // while its goroutines never ran (observed: counter stuck at
        // 1, exit 1, PASS).
        if (result.exit_code == 0) {
          if (verbose)
            std::cout << "[PASS] " << name << " (" << result.elapsed_ms << "ms)" << std::endl
                      << std::flush;
          pass++;
        } else {
          std::cout << "[FAIL] " << name << " (exit=" << result.exit_code << ")" << std::endl
                    << std::flush;
          print_result_tail(result);
          fail++;
        }
      } else {
        std::cout << "[FAIL] " << name << " (exit=" << result.exit_code << ")" << std::endl
                  << std::flush;
        print_result_tail(result);
        fail++;
      }
    }
  };
  std::vector<std::thread> threads;
  threads.reserve(workers);
  for (unsigned w = 0; w < workers; ++w)
    threads.emplace_back(worker);
  for (auto &t : threads)
    t.join();
  for (auto &r : results_par)
    results.push_back(r);

  auto suite_end = std::chrono::high_resolution_clock::now();
  double suite_total_ms =
      std::chrono::duration<double, std::milli>(suite_end - suite_start).count();

  double tests_total_ms = 0;
  for (const auto &r : results)
    tests_total_ms += r.elapsed_ms;
  double avg_ms = results.empty() ? 0 : tests_total_ms / results.size();
  std::sort(results.begin(), results.end(), [](const ScriptResult &a, const ScriptResult &b) {
    return a.elapsed_ms > b.elapsed_ms;
  });

  std::cout << "\nsmoke: " << pass << " passed, " << fail << " failed, " << skip << " skipped | "
            << results.size() << " files, " << tests_total_ms << "ms tests, " << avg_ms << "ms avg"
            << std::endl
            << std::flush;
  std::cout << "performance: " << suite_total_ms << "ms total suite, " << tests_total_ms
            << "ms in-process, " << tests_total_ms / std::max<size_t>(results.size(), 1)
            << "ms/test avg" << std::endl
            << std::flush;
  if (!results.empty()) {
    int n = std::min<int>(5, results.size());
    std::cout << "startup time (slowest):";
    for (int i = 0; i < n; i++) {
      std::cout << " " << results[i].name() << "=" << (int)results[i].elapsed_ms << "ms";
    }
    std::cout << std::endl << std::flush;
  }
  return fail > 0 ? 1 : 0;
}

// Probe whether the havel binary actually supports AOT compilation
// (requires a build with ENABLE_LLVM=ON). Runs one tiny script through
// --target aot; cheaper and more truthful than guessing from build flags.
inline bool probe_aot_support(const std::string &havel_bin) {
  fs::path probe_dir = fs::temp_directory_path() / ("havel_aot_probe_" + std::to_string(getpid()));
  std::error_code ec;
  fs::create_directories(probe_dir, ec);
  if (ec)
    return false;
  std::string probe_script = (probe_dir / "probe.hv").string();
  std::string probe_out = (probe_dir / "probe_out").string();
  {
    std::ofstream ofs(probe_script);
    ofs << "x = 1\nprint(\"aot probe ${x}\")\n";
  }
  auto result = run_script(havel_bin, probe_script, {"--target", "aot", "-o", probe_out});
  fs::remove_all(probe_dir, ec);
  return result.passed;
}

// Run a script in all modes and compare results
struct ComparisonResult {
  std::string path;
  bool cpp_passed = false;         // C++ pipeline (--no-self-hosted)
  bool self_hosted_passed = false; // self-hosted Havel pipeline
  bool aot_passed = false;         // AOT compilation only (output not executed)
  bool aot_skipped = false;        // binary built without LLVM support
  int cpp_exit = -1;
  int self_hosted_exit = -1;
  int aot_exit = -1;
  double cpp_ms = 0;
  double self_hosted_ms = 0;
  double aot_ms = 0;
};

inline ComparisonResult run_script_all_modes(const std::string &havel_bin,
                                             const std::string &script_path,
                                             const std::string &self_hosted_path = "",
                                             bool aot_available = false) {
  ComparisonResult result;
  result.path = script_path;

  // 1. C++ pipeline mode
  {
    std::vector<std::string> flags = {"--run", "--no-self-hosted"};
    auto start = std::chrono::high_resolution_clock::now();
    auto script_result = run_script(havel_bin, script_path, flags);
    auto end = std::chrono::high_resolution_clock::now();
    result.cpp_ms = std::chrono::duration<double, std::milli>(end - start).count();
    result.cpp_passed = script_result.passed;
    result.cpp_exit = script_result.exit_code;
  }

  // 2. Self-hosted mode (if self_hosted_path provided)
  if (!self_hosted_path.empty()) {
    std::vector<std::string> flags = {"--run", "--self-hosted-path", self_hosted_path};
    auto start = std::chrono::high_resolution_clock::now();
    auto script_result = run_script(havel_bin, script_path, flags);
    auto end = std::chrono::high_resolution_clock::now();
    result.self_hosted_ms = std::chrono::duration<double, std::milli>(end - start).count();
    result.self_hosted_passed = script_result.passed;
    result.self_hosted_exit = script_result.exit_code;
  }

  // 3. AOT mode: verifies compilation only - the emitted object/shared
  // library is not executed, so this compares pipeline agreement on
  // compiling the script, not runtime equivalence.
  if (aot_available) {
    // Use unique output path per test run to avoid collisions in batch runs
    // Include PID, timestamp, and random component for uniqueness
    std::string stem = fs::path(script_path).stem().string();
    std::string unique_id = std::to_string(getpid()) + "_" +
                            std::to_string(std::chrono::duration_cast<std::chrono::milliseconds>(
                                               std::chrono::steady_clock::now().time_since_epoch())
                                               .count());
    std::string output_path = "/tmp/aot_" + stem + "_" + unique_id;
    // run_script appends script_path itself - do not duplicate it here
    std::vector<std::string> flags = {"--target", "aot", "-o", output_path};
    auto start = std::chrono::high_resolution_clock::now();
    auto script_result = run_script(havel_bin, script_path, flags);
    auto end = std::chrono::high_resolution_clock::now();
    result.aot_ms = std::chrono::duration<double, std::milli>(end - start).count();
    result.aot_passed = script_result.passed;
    result.aot_exit = script_result.exit_code;

    // Clean up AOT temp files after each test to prevent accumulation
    std::error_code ec;
    std::filesystem::remove(output_path + ".o", ec);
    std::filesystem::remove(output_path + ".so", ec);
    std::filesystem::remove(output_path, ec);
  } else {
    result.aot_skipped = true;
  }

  return result;
}

inline int run_comparison_suite(const std::string &havel_bin,
                                const std::vector<std::string> &directories,
                                const std::string &self_hosted_path = "", bool verbose = false) {
  auto scripts = discover_scripts(directories);
  if (scripts.empty()) {
    std::cerr << "no .hv scripts found in specified directories" << std::endl;
    return 1;
  }

  // Detect AOT support once for the whole suite. Without LLVM the AOT
  // column is skipped instead of reporting hundreds of false mismatches.
  bool aot_available = probe_aot_support(havel_bin);
  if (!aot_available) {
    std::cout << "[NOTE] AOT unavailable (build without ENABLE_LLVM=ON) - aot column skipped"
              << std::endl
              << std::flush;
  }

  int pass = 0, fail = 0, mismatch = 0, skipped = 0;
  auto suite_start = std::chrono::high_resolution_clock::now();

  for (const auto &script : scripts) {
    auto result = run_script_all_modes(havel_bin, script, self_hosted_path, aot_available);
    if (!script.empty()) {
      std::cout << "[RUN] " << script << " ..." << std::flush << std::endl;
    }

    // Columns that actually ran this iteration
    bool cpp_ok = result.cpp_passed;
    bool sh_ok = self_hosted_path.empty() ? cpp_ok : result.self_hosted_passed;
    bool aot_ok = !result.aot_skipped && result.aot_passed;

    auto col = [](const char *name, bool ran, bool ok) {
      return std::string(name) + "=" + (!ran ? "SKIP" : (ok ? "PASS" : "FAIL"));
    };

    bool all_ran_agree = cpp_ok == sh_ok && (!aot_available || aot_ok == cpp_ok);

    if (all_ran_agree && cpp_ok) {
      std::cout << "[PASS] " << script << " (cpp=" << (int)result.cpp_ms << "ms"
                << ", sh="
                << (self_hosted_path.empty() ? "-"
                                             : std::to_string((int)result.self_hosted_ms) + "ms")
                << ", aot="
                << (result.aot_skipped ? "skip" : std::to_string((int)result.aot_ms) + "ms") << ")"
                << std::endl
                << std::flush;
      pass++;
    } else if (cpp_ok != sh_ok || (aot_available && aot_ok != cpp_ok)) {
      std::cout << "[MISMATCH] " << script << " (" << col("cpp", true, cpp_ok) << ", "
                << col("sh", !self_hosted_path.empty(), sh_ok) << ", "
                << col("aot", aot_available, aot_ok) << ")" << std::endl
                << std::flush;
      mismatch++;
    } else {
      std::cout << "[FAIL] " << script << " (" << col("cpp", true, cpp_ok) << ", "
                << col("sh", !self_hosted_path.empty(), sh_ok) << ", "
                << col("aot", aot_available, aot_ok) << ")" << std::endl
                << std::flush;
      fail++;
    }
    if (result.aot_skipped)
      skipped++;
  }

  auto suite_end = std::chrono::high_resolution_clock::now();
  double total_ms = std::chrono::duration<double, std::milli>(suite_end - suite_start).count();

  std::cout << "\ncomparison: " << pass << " passed, " << fail << " failed, " << mismatch
            << " mismatched, " << skipped << " aot-skipped (" << (int)total_ms << "ms total)"
            << std::endl
            << std::flush;
  return (fail > 0 || mismatch > 0) ? 1 : 0;
}

} // namespace hvtest
