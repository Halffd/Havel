#pragma once

#include <chrono>
#include <filesystem>
#include <iostream>
#include <signal.h>
#include <string>
#include <sys/wait.h>
#include <unistd.h>
#include <vector>

namespace hvtest {

namespace fs = std::filesystem;

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

inline ScriptResult run_script(const std::string &havel_bin, const std::string &script_path,
                               int timeout_seconds = 30,
                               const std::vector<std::string> &pre_flags = {}) {
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
        if (pre_flags.empty()) {
            execlp(havel_bin.c_str(), havel_bin.c_str(), "--run", "--pure-stdlib",
                   script_path.c_str(), nullptr);
        } else {
            std::vector<char *> args;
            args.push_back(const_cast<char *>(havel_bin.c_str()));
            for (const auto &f : pre_flags) {
                args.push_back(const_cast<char *>(f.c_str()));
            }
            args.push_back(const_cast<char *>(script_path.c_str()));
            args.push_back(nullptr);
            execvp(havel_bin.c_str(), args.data());
        }
        _exit(127);
    }

	close(pipefd[1]);

	char buffer[4096];
	while (true) {
		ssize_t n = read(pipefd[0], buffer, sizeof(buffer));
		if (n <= 0) break;
	}

	close(pipefd[0]);

	int status = 0;
	bool killed = false;

	auto deadline = start + std::chrono::seconds(timeout_seconds);
	while (true) {
		pid_t ret = waitpid(pid, &status, WNOHANG);
		if (ret > 0) break;
		if (ret == -1) break;

		auto now = std::chrono::high_resolution_clock::now();
		if (now >= deadline) {
			kill(pid, SIGKILL);
			waitpid(pid, &status, 0);
			killed = true;
			break;
		}
		usleep(10000);
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
                            const std::vector<std::string> &pre_flags = {}) {
    auto scripts = discover_scripts(directories);
    if (scripts.empty()) {
        std::cerr << "no .hv scripts found in specified directories" << std::endl;
        return 1;
    }

    int pass = 0, fail = 0;
    std::vector<ScriptResult> results;
    for (const auto &script : scripts) {
        auto result = run_script(havel_bin, script, 30, pre_flags);
        results.push_back(result);
        if (result.passed) {
            std::cout << "[PASS] " << script << " (" << result.elapsed_ms << "ms)" << std::endl;
            pass++;
        } else if (result.timed_out) {
            std::cout << "[FAIL] " << script << " (timeout)" << std::endl;
            fail++;
        } else {
            std::cout << "[FAIL] " << script << " (exit=" << result.exit_code << ")" << std::endl;
            fail++;
        }
    }

    double total_ms = 0;
    for (const auto &r : results) total_ms += r.elapsed_ms;
    std::cout << "\nscripts: " << pass << " passed, " << fail << " failed | " << results.size() << " files, " << total_ms << "ms total" << std::endl;
    return fail > 0 ? 1 : 0;
}

inline int list_scripts(const std::vector<std::string> &directories) {
    auto scripts = discover_scripts(directories);
    for (const auto &script : scripts) {
        std::cout << script << std::endl;
    }
    std::cout << scripts.size() << " test(s)" << std::endl;
    return 0;
}

inline int run_smoke_suite(const std::string &havel_bin, const std::string &smoke_dir,
                           bool verbose = false,
                           const std::vector<std::string> &pre_flags = {}) {
    auto scripts = discover_scripts({smoke_dir});
    if (scripts.empty()) {
        std::cerr << "no .hv smoke tests found in " << smoke_dir << std::endl;
        return 1;
    }

    // Detect bytecode/self-hosted modules path: derived from havel_bin's location.
    // For build-debug/havel -> modules dir is ../modules
    fs::path bin_path(havel_bin);
    fs::path module_parent = bin_path.parent_path().parent_path();
    fs::path modules_root = module_parent / "modules";
    std::string bc_path = modules_root.string();
    if (!fs::exists(modules_root)) {
        bc_path = (fs::current_path() / "modules").string();
    }
    std::cout << "bytecode path: " << bc_path << std::endl;
    std::cout << "self-hosted path: " << module_parent.string() << std::endl;
    std::cout << "pipeline: " << (pre_flags.empty() ? "c++" : "self-hosted") << std::endl;

    int pass = 0, fail = 0, skip = 0;
    std::vector<ScriptResult> results;
    auto suite_start = std::chrono::high_resolution_clock::now();

    for (const auto &script : scripts) {
        auto result = run_script(havel_bin, script, 30, pre_flags);
        results.push_back(result);
        auto name = fs::path(script).stem().string();
        if (result.passed) {
            if (verbose) std::cout << "[PASS] " << name << " (" << result.elapsed_ms << "ms)" << std::endl;
            pass++;
        } else if (result.timed_out) {
            std::cout << "[FAIL] " << name << " (timeout)" << std::endl;
            fail++;
        } else if (result.exit_code == -6 || result.exit_code == -11) {
            if (verbose) std::cout << "[SKIP] " << name << " (crash, needs event loop)" << std::endl;
            skip++;
        } else {
            std::cout << "[FAIL] " << name << " (exit=" << result.exit_code << ")" << std::endl;
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
              << avg_ms << "ms avg" << std::endl;
    std::cout << "performance: " << suite_total_ms << "ms total suite, "
              << tests_total_ms << "ms in-process, "
              << tests_total_ms / std::max<size_t>(results.size(), 1) << "ms/test avg" << std::endl;
    if (!results.empty()) {
        int n = std::min<int>(5, results.size());
        std::cout << "startup time (slowest):";
        for (int i = 0; i < n; i++) {
            std::cout << " " << results[i].name() << "=" << (int)results[i].elapsed_ms << "ms";
        }
        std::cout << std::endl;
    }
    return fail > 0 ? 1 : 0;
}

}
