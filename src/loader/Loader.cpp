/*
 * Loader.cpp - Unified Loader implementation
 *
 * Script resolution logic (resolve, cache) migrated from runtime/ModuleLoader.cpp
 * Native dlopen logic lives in Loader.c (pure C)
 */

#include "Loader.hpp"
#include <filesystem>
#include <cstdlib>

namespace havel {

std::optional<Loader::ResolvedModule>
Loader::resolve(const std::string &modulePath,
		const std::string &scriptDir) const {
	namespace fs = std::filesystem;

	std::string name = modulePath;

	if (fs::path(modulePath).is_absolute()) {
		if (fs::exists(modulePath)) {
			return ResolvedModule{ResolvedModule::UserSource, modulePath, modulePath};
		}
		fs::path hvcPath = fs::path(modulePath).replace_extension(".hvc");
		if (fs::exists(hvcPath)) {
			return ResolvedModule{ResolvedModule::BytecodeCache,
				fs::canonical(hvcPath).string(), modulePath};
		}
		return std::nullopt;
	}

	if (modulePath.starts_with("./") || modulePath.starts_with("../")) {
		fs::path resolved = fs::path(scriptDir) / modulePath;
		if (fs::exists(resolved)) {
			return ResolvedModule{ResolvedModule::UserSource,
				fs::canonical(resolved).string(), modulePath};
		}
		fs::path hvcPath = fs::path(scriptDir) / (modulePath + ".hvc");
		if (modulePath.ends_with(".hv")) {
			hvcPath = fs::path(scriptDir) /
				(modulePath.substr(0, modulePath.size() - 3) + ".hvc");
		}
		if (fs::exists(hvcPath)) {
			return ResolvedModule{ResolvedModule::BytecodeCache,
				fs::canonical(hvcPath).string(), modulePath};
		}
		return std::nullopt;
	}

	if (scriptCache_.count(modulePath) > 0) {
		return ResolvedModule{ResolvedModule::Cached, "", modulePath};
	}

	auto pickHvOrHvc = [&](const fs::path &basePath) -> std::optional<ResolvedModule> {
		fs::path hvPath = fs::path(basePath) / (name + ".hv");
		fs::path hvcPath = fs::path(basePath) / (name + ".hvc");
		bool hvExists = fs::exists(hvPath);
		bool hvcExists = fs::exists(hvcPath);
		if (hvcExists && hvExists) {
			auto hvcTime = fs::last_write_time(hvcPath);
			auto hvTime = fs::last_write_time(hvPath);
			if (hvcTime >= hvTime) {
				return ResolvedModule{ResolvedModule::BytecodeCache,
					fs::canonical(hvcPath).string(), modulePath};
			}
			return ResolvedModule{ResolvedModule::UserSource,
				fs::canonical(hvPath).string(), modulePath};
		}
		if (hvcExists) {
			return ResolvedModule{ResolvedModule::BytecodeCache,
				fs::canonical(hvcPath).string(), modulePath};
		}
		if (hvExists) {
			return ResolvedModule{ResolvedModule::UserSource,
				fs::canonical(hvPath).string(), modulePath};
		}
		return std::nullopt;
	};

	if (!scriptDir.empty()) {
		fs::path scriptDirPath(scriptDir);

		auto local = pickHvOrHvc(scriptDirPath);
		if (local) return local;

		fs::path pkgDir = scriptDirPath / name;
		auto pkg = pickHvOrHvc(pkgDir);
		if (pkg) return pkg;
	}

	if (!scriptDir.empty()) {
		fs::path cachePath = fs::path(scriptDir) / "__cache__" / (name + ".hvc");
		if (fs::exists(cachePath)) {
			return ResolvedModule{ResolvedModule::BytecodeCache,
				fs::canonical(cachePath).string(), modulePath};
		}
		fs::path hbcPath = fs::path(scriptDir) / "__cache__" / (name + ".hbc");
		if (fs::exists(hbcPath)) {
			return ResolvedModule{ResolvedModule::BytecodeCache,
				fs::canonical(hbcPath).string(), modulePath};
		}
	}

	if (!stdlibPath_.empty()) {
		fs::path stdlibHvcPath = fs::path(stdlibPath_) / (name + ".hvc");
		fs::path stdlibHvPath = fs::path(stdlibPath_) / (name + ".hv");
		bool hvcExists = fs::exists(stdlibHvcPath);
		bool hvExists = fs::exists(stdlibHvPath);
		if (hvcExists && hvExists) {
			auto hvcTime = fs::last_write_time(stdlibHvcPath);
			auto hvTime = fs::last_write_time(stdlibHvPath);
			if (hvcTime >= hvTime) {
				return ResolvedModule{ResolvedModule::BytecodeCache,
					fs::canonical(stdlibHvcPath).string(), modulePath};
			}
			return ResolvedModule{ResolvedModule::StdlibSource,
				fs::canonical(stdlibHvPath).string(), modulePath};
		}
		if (hvcExists) {
			return ResolvedModule{ResolvedModule::BytecodeCache,
				fs::canonical(stdlibHvcPath).string(), modulePath};
		}
		if (hvExists) {
			return ResolvedModule{ResolvedModule::StdlibSource,
				fs::canonical(stdlibHvPath).string(), modulePath};
		}
	}

	if (const char *home = std::getenv("HOME")) {
		fs::path pkgDir = fs::path(home) / ".havel" / "packages" / name;
		fs::path pkgHvcPath = pkgDir / (name + ".hvc");
		fs::path pkgHvPath = pkgDir / (name + ".hv");
		bool hvcExists = fs::exists(pkgHvcPath);
		bool hvExists = fs::exists(pkgHvPath);
		if (hvcExists && hvExists) {
			auto hvcTime = fs::last_write_time(pkgHvcPath);
			auto hvTime = fs::last_write_time(pkgHvPath);
			if (hvcTime >= hvTime) {
				return ResolvedModule{ResolvedModule::BytecodeCache,
					fs::canonical(pkgHvcPath).string(), modulePath};
			}
			return ResolvedModule{ResolvedModule::PackageSource,
				fs::canonical(pkgHvPath).string(), modulePath};
		}
		if (hvcExists) {
			return ResolvedModule{ResolvedModule::BytecodeCache,
				fs::canonical(pkgHvcPath).string(), modulePath};
		}
		if (hvExists) {
			return ResolvedModule{ResolvedModule::PackageSource,
				fs::canonical(pkgHvPath).string(), modulePath};
		}
	}

	for (const auto &sp : scriptSearchPaths_) {
		fs::path spDir(sp);

		auto found = pickHvOrHvc(spDir);
		if (found) return found;

		fs::path pkgDir = spDir / name;
		auto pkg = pickHvOrHvc(pkgDir);
		if (pkg) return pkg;
	}

	for (const auto &sp : scriptSearchPaths_) {
		fs::path spDir(sp);

		fs::path soPath = spDir / (name + ".so");
		if (fs::exists(soPath)) {
			return ResolvedModule{ResolvedModule::NativeExtension,
				fs::canonical(soPath).string(), modulePath};
		}

		fs::path libPath = spDir / ("libhavel_" + name + ".so");
		if (fs::exists(libPath)) {
			return ResolvedModule{ResolvedModule::NativeExtension,
				fs::canonical(libPath).string(), modulePath};
		}
	}

	return std::nullopt;
}

bool Loader::isCached(const std::string &key) const {
	return scriptCache_.count(key) > 0;
}

bool Loader::getCached(const std::string &key, core::Value *outValue) const {
	auto it = scriptCache_.find(key);
	if (it == scriptCache_.end()) return false;
	if (outValue) *outValue = it->second;
	return true;
}

void Loader::putCache(const std::string &key, core::Value value) {
	scriptCache_[key] = value;
}

void Loader::clearCache() {
	scriptCache_.clear();
}

} // namespace havel
