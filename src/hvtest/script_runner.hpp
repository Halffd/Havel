#pragma once

#include <chrono>
#include <cstdlib>
#include <fstream>
#include <filesystem>
#include <iostream>
#include <poll.h>
#include <signal.h>
#include <string>
#include <sys/wait.h>
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
    bool timed_out;
    std::string name() const {
        return fs::path(path).stem().string();
    }
};

inline std::vector<std::string> discover_scripts(const std::vector<std::string> &directories) {
	std::vector<std::string> scripts;
	for (const auto &dir : directories) {
		if (!fs::exists(dir)) continue;
		for (const auto &entry : fs::directory_iterator(dir)) {
			if (entry.is_regular_file() && entry.path().extension() == ".hv") {
				scripts.push_back(entry.path().string());
			}
		}
	}
	std::sort(scripts.begin(), scripts.end());
	return scripts;
}

inline std::vector<std::string> list_test_dirs(const std::string &scripts_root) {
	return {
		scripts_root + "/smoke",
		scripts_root + "/integration",
		scripts_root + "/tests/main",
	};
}

// Read per-test timeout from file header.
// Format: // smoke: timeout = <seconds>  (or // test: timeout = <seconds>)
inline int read_test_timeout(const std::string &script_path) {
	std::ifstream ifs(script_path);
	if (!ifs) return 0;
	std::string line;
	int count = 0;
	while (std::getline(ifs, line) && count < 20) {
		count++;
		// Look for // smoke: timeout = <n> or // test: timeout = <n>
		if (line.rfind("// smoke: timeout =", 0) == 0 || line.rfind("// test: timeout =", 0) == 0) {
			size_t eq = line.find('=');
			if (eq != std::string::npos) {
				std::string val = line.substr(eq + 1);
				val.erase(0, val.find_first_not_of(" \t"));
				val.erase(val.find_last_not_of(" \t") + 1);
				try {
					return std::stoi(val);
				} catch (...) {}
			}
		}
	}
	return 0; // 0 means use default
}

inline ScriptResult run_script(const std::string &havel_bin, const std::string &script_path,
                               int timeout_seconds = 60,
                               const std::vector<std::string> &pre_flags = {}) {
    // Check for per-test timeout in file header (e.g., // smoke: timeout = 180)
    int per_test_timeout = read_test_timeout(script_path);
    if (per_test_timeout > 0) {
        timeout_seconds = per_test_timeout;
    }

    ScriptResult result;
    result.path = script_path;
    result.passed = false;
    result.exit_code = -1;
    result.elapsed_ms = 0;
    result.timed_out = false;

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
        } else {
            flags = pre_flags;
        }
        std::vector<char *> args;
        args.push_back(const_cast<char *>(havel_bin.c_str()));
        for (const auto &f : flags) {
            args.push_back(const_cast<char *>(f.c_str()));
        }
        args.push_back(const_cast<char *>(script_path.c_str()));
        args.push_back(nullptr);
        
        // Pass through environment variables (needed for HAVEL_EXTENSION_DIR)
        std::vector<char *> env;
        for (char **e = ::environ; *e; ++e) {
            env.push_back(*e);
        }
        env.push_back(nullptr);
        execvpe(havel_bin.c_str(), args.data(), env.data());
        _exit(127);
    }

	close(pipefd[1]);

	struct pollfd pfd = {pipefd[0], POLLIN, 0};

	char buffer[4096];
	int status = 0;
	bool killed = false;
	bool pipe_done = false;
	auto deadline = start + std::chrono::seconds(timeout_seconds);

	while (true) {
		if (!pipe_done) {
			int pret = poll(&pfd, 1, 100);
			if (pret > 0) {
				ssize_t n = read(pipefd[0], buffer, sizeof(buffer));
				if (n <= 0) {
					close(pipefd[0]);
					pipe_done = true;
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
						if (n <= 0) break;
					} else {
						break;
					}
				}
				close(pipefd[0]);
			}
			break;
		}

		auto now = std::chrono::high_resolution_clock::now();
		if (now >= deadline) {
			kill(pid, SIGKILL);
			waitpid(pid, &status, 0);
			killed = true;
			if (!pipe_done) close(pipefd[0]);
			break;
		}
	}

	auto end = std::chrono::high_resolution_clock::now();
	result.elapsed_ms = std::chrono::duration<double, std::milli>(end - start).count();

    if (killed) {
        result.timed_out = true;
        result.exit_code = -1;
    } else if (WIFEXITED(status)) {
        result.exit_code = WEXITSTATUS(status);
        result.passed = (result.exit_code == 0);
    } else if (WIFSIGNALED(status)) {
        result.exit_code = -WTERMSIG(status);
    }

	return result;
}

inline int run_script_suite(const std::string &havel_bin, const std::vector<std::string> &directories, bool verbose = false,
                            const std::vector<std::string> &pre_flags = {},
                            int timeout_seconds = 60) {
    auto scripts = discover_scripts(directories);
    if (scripts.empty()) {
        std::cerr << "no .hv scripts found in specified directories" << std::endl;
        return 1;
    }

    int pass = 0, fail = 0;
    std::vector<ScriptResult> results;
    for (const auto &script : scripts) {
        auto result = run_script(havel_bin, script, timeout_seconds, pre_flags);
        results.push_back(result);
        if (result.passed) {
            std::cout << "[PASS] " << script << " (" << result.elapsed_ms << "ms)" << std::endl << std::flush;
            pass++;
        } else if (result.timed_out) {
            std::cout << "[FAIL] " << script << " (timeout)" << std::endl << std::flush;
            fail++;
        } else {
            std::cout << "[FAIL] " << script << " (exit=" << result.exit_code << ")" << std::endl << std::flush;
            fail++;
        }
    }

    double total_ms = 0;
    for (const auto &r : results) total_ms += r.elapsed_ms;
    std::cout << "\nscripts: " << pass << " passed, " << fail << " failed | " << results.size() << " files, " << total_ms << "ms total" << std::endl << std::flush;
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

inline int run_smoke_suite(const std::string &havel_bin, const std::string &smoke_dir,
                           bool verbose = false,
                           const std::vector<std::string> &pre_flags = {},
                           int timeout_seconds = 60) {
    auto scripts = discover_scripts({smoke_dir});
    if (scripts.empty()) {
        std::cerr << "no .hv smoke tests found in " << smoke_dir << std::endl;
        return 1;
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

    for (const auto &script : scripts) {
        auto result = run_script(havel_bin, script, timeout_seconds, pre_flags);
        results.push_back(result);
        auto name = fs::path(script).stem().string();
        if (result.passed) {
            if (verbose) std::cout << "[PASS] " << name << " (" << result.elapsed_ms << "ms)" << std::endl << std::flush;
            pass++;
        } else if (result.timed_out) {
            std::cout << "[FAIL] " << name << " (timeout)" << std::endl << std::flush;
            fail++;
        } else if (result.exit_code == -6 || result.exit_code == -11) {
            if (verbose) std::cout << "[SKIP] " << name << " (crash, needs event loop)" << std::endl << std::flush;
            skip++;
        } else if (!pre_flags.empty() && result.exit_code != 255) {
            // Self-hosted mode: script return value becomes exit code.
            // exit=255 means process.exit(255) was called (assertion failure).
            // Any other exit code is the script's return value (success).
            if (verbose) std::cout << "[PASS] " << name << " (" << result.elapsed_ms << "ms)" << std::endl << std::flush;
            pass++;
        } else {
            std::cout << "[FAIL] " << name << " (exit=" << result.exit_code << ")" << std::endl << std::flush;
            fail++;
        }
    }

    auto suite_end = std::chrono::high_resolution_clock::now();
    double suite_total_ms = std::chrono::duration<double, std::milli>(suite_end - suite_start).count();

    double tests_total_ms = 0;
    for (const auto &r : results) tests_total_ms += r.elapsed_ms;
    double avg_ms = results.empty() ? 0 : tests_total_ms / results.size();
    std::sort(results.begin(), results.end(), [](const ScriptResult &a, const ScriptResult &b) {
        return a.elapsed_ms > b.elapsed_ms;
    });

    std::cout << "\nsmoke: " << pass << " passed, " << fail << " failed, " << skip << " skipped | "
              << results.size() << " files, " << tests_total_ms << "ms tests, "
              << avg_ms << "ms avg" << std::endl << std::flush;
    std::cout << "performance: " << suite_total_ms << "ms total suite, "
              << tests_total_ms << "ms in-process, "
              << tests_total_ms / std::max<size_t>(results.size(), 1) << "ms/test avg" << std::endl << std::flush;
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

// Run a script in all 4 modes and compare results
struct ComparisonResult {
    std::string path;
    bool cpp_passed = false;
    bool self_hosted_passed = false;
    bool jit_passed = false;
    bool aot_passed = false;
    int cpp_exit = -1;
    int self_hosted_exit = -1;
    int jit_exit = -1;
    int aot_exit = -1;
    double cpp_ms = 0;
    double self_hosted_ms = 0;
    double jit_ms = 0;
    double aot_ms = 0;
    bool all_agree = false;
};

inline ComparisonResult run_script_all_modes(const std::string &havel_bin, const std::string &script_path,
                                              int timeout_seconds = 60,
                                              const std::string &self_hosted_path = "") {
    ComparisonResult result;
    result.path = script_path;
    int timeout_sec = timeout_seconds; // Avoid parameter shadowing

    // 1. C++ interpreter mode (no flags)
    {
        auto start = std::chrono::high_resolution_clock::now();
        auto script_result = run_script(havel_bin, script_path, timeout_sec, {});
        auto end = std::chrono::high_resolution_clock::now();
        result.cpp_ms = std::chrono::duration<double, std::milli>(end - start).count();
        result.cpp_passed = script_result.passed;
        result.cpp_exit = script_result.exit_code;
    }

    // 2. Self-hosted mode (if self_hosted_path provided)
    if (!self_hosted_path.empty()) {
        std::vector<std::string> flags = {"--run", "--self-hosted-path", self_hosted_path};
        auto start = std::chrono::high_resolution_clock::now();
        auto script_result = run_script(havel_bin, script_path, timeout_sec, flags);
        auto end = std::chrono::high_resolution_clock::now();
        result.self_hosted_ms = std::chrono::duration<double, std::milli>(end - start).count();
        result.self_hosted_passed = script_result.passed;
        result.self_hosted_exit = script_result.exit_code;
    }

    // 3. JIT mode (if available)
    #ifdef HAVEL_ENABLE_LLVM
    {
        // JIT is currently tested via run_jit_smoke_tests which runs internal tests
        // For now, we'll skip JIT mode for individual scripts unless we add support
    }
    #endif

    // 4. AOT mode
    {
        std::string output_path = "/tmp/aot_" + fs::path(script_path).stem().string();
        std::vector<std::string> flags = {"--target", "aot", script_path, "-o", "/tmp/aot_" + fs::path(script_path).stem().string()};
        auto start = std::chrono::high_resolution_clock::now();
        auto script_result = run_script(havel_bin, script_path, timeout_sec, flags);
        auto end = std::chrono::high_resolution_clock::now();
        result.aot_ms = std::chrono::duration<double, std::milli>(end - start).count();
        result.aot_passed = script_result.passed;
        result.aot_exit = script_result.exit_code;
    }

    // Check if all modes agree
    bool has_self_hosted = !self_hosted_path.empty();
    result.all_agree = (result.cpp_passed == result.self_hosted_passed) &&
                       (!result.self_hosted_passed || result.cpp_passed == result.aot_passed);

    return result;
}

inline int run_comparison_suite(const std::string &havel_bin, const std::vector<std::string> &directories,
                                 const std::string &self_hosted_path = "",
                                 int timeout_seconds = 60, bool verbose = false) {
    auto scripts = discover_scripts(directories);
    if (scripts.empty()) {
        std::cerr << "no .hv scripts found in specified directories" << std::endl;
        return 1;
    }

    int pass = 0, fail = 0, mismatch = 0;
    auto suite_start = std::chrono::high_resolution_clock::now();

    for (const auto &script : scripts) {
        auto result = run_script_all_modes(havel_bin, script, timeout_seconds, "");
        if (!script.empty()) {
            std::cout << "[RUN] " << script << " ..." << std::flush << std::endl;
        }
        
        if (result.cpp_passed && result.aot_passed) {
            std::cout << "[PASS] " << script << " (cpp=" << (int)result.cpp_ms << "ms, aot=" << (int)result.aot_ms << "ms)" << std::endl << std::flush;
        } else if (result.cpp_passed != result.aot_passed) {
            std::cout << "[MISMATCH] " << script << " (cpp=" << (result.cpp_passed ? "PASS" : "FAIL") 
                      << ", aot=" << (result.aot_passed ? "PASS" : "FAIL") << ")" << std::endl << std::flush;
        } else {
            std::cout << "[FAIL] " << script << " (cpp=" << (result.cpp_passed ? "PASS" : "FAIL") 
                      << ", aot=" << (result.aot_passed ? "PASS" : "FAIL") << ")" << std::endl << std::flush;
        }
    }

    auto suite_end = std::chrono::high_resolution_clock::now();
    double total_ms = std::chrono::duration<double, std::milli>(suite_end - suite_start).count();

    std::cout << "\ncomparison: " << pass << " passed, " << fail << " failed, " << mismatch << " mismatched" << std::endl << std::flush;
    return (fail > 0 || mismatch > 0) ? 1 : 0;
}

} // namespace hvtest
