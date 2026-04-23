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

inline ScriptResult run_script(const std::string &havel_bin, const std::string &script_path, int timeout_seconds = 30) {
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
		execlp(havel_bin.c_str(), havel_bin.c_str(), script_path.c_str(), nullptr);
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
	}

	return result;
}

inline int run_script_suite(const std::string &havel_bin, const std::vector<std::string> &directories, bool verbose = false) {
	auto scripts = discover_scripts(directories);
	if (scripts.empty()) {
		std::cerr << "no .hv scripts found in specified directories" << std::endl;
		return 1;
	}

	int pass = 0, fail = 0;
	for (const auto &script : scripts) {
		auto result = run_script(havel_bin, script);
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

	std::cout << "\nscripts: " << pass << " passed, " << fail << " failed" << std::endl;
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

}
