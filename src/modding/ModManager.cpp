#include "ModManager.hpp"

#include "c/LoggerC.h"
#include "havel/mod.h"
#include "modding/hook.h"

#include <dlfcn.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>

#include <cctype>
#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <stdexcept>

namespace fs = std::filesystem;

namespace havel::modding {

/* ---------------------------------------------------------------------------
 * HavelModApi thunks. They run while ModManager::load() is calling a mod's
 * havel_mod_init(), so installing_ names that mod and any hook it installs
 * is attributed to it (and removed automatically when it unloads).
 * ------------------------------------------------------------------------- */

static void api_log(const char *message) {
	HAVEL_LOGF_INFO("mod: %s", message ? message : "(null)");
}

static void *api_resolve(const char *symbol) {
	if (!symbol) return nullptr;
	return ModManager::instance().resolve("", symbol);
}

static int api_hook_install(const char *symbol, void *replacement,
                            void **trampoline, char *err, size_t errsz) {
	if (err && errsz) err[0] = '\0';
	if (!symbol || !replacement) {
		if (err && errsz) snprintf(err, errsz, "mod api: hook_install needs a symbol and a replacement");
		return -1;
	}
	try {
		void *tp = ModManager::instance().hookFromMod(symbol, replacement);
		if (trampoline) *trampoline = tp;
		if (err && errsz) err[0] = '\0';
		return 0;
	} catch (const std::exception &e) {
		if (err && errsz) snprintf(err, errsz, "%s", e.what());
		return -1;
	}
}

static int api_hook_remove(const char *symbol, char *err, size_t errsz) {
	if (err && errsz) err[0] = '\0';
	if (!symbol) {
		if (err && errsz) snprintf(err, errsz, "mod api: hook_remove needs a symbol");
		return -1;
	}
	bool ok = ModManager::instance().unhook(symbol);
	if (!ok && err && errsz)
		snprintf(err, errsz, "no hook installed for '%s' (already removed is fine)", symbol);
	return ok ? 0 : -1;
}

ModManager::ModManager() {
	api_.abi_version = HAVEL_MOD_ABI_VERSION;
	api_.ctx = nullptr;
	api_.log = api_log;
	api_.hook_install = api_hook_install;
	api_.hook_remove = api_hook_remove;
	api_.resolve = api_resolve;
}

ModManager &ModManager::instance() {
	static ModManager mgr;
	return mgr;
}

ModManager::ModRecord *ModManager::find(const std::string &name) {
	for (auto &m : mods_) if (m->name == name) return m.get();
	return nullptr;
}

const ModManager::ModRecord *ModManager::find(const std::string &name) const {
	for (const auto &m : mods_) if (m->name == name) return m.get();
	return nullptr;
}

std::string ModManager::generationPath(const std::string &name, int gen) const {
	auto dir = fs::temp_directory_path() / "havel_mods";
	std::error_code ec;
	fs::create_directories(dir, ec);
	char buf[1024];
	snprintf(buf, sizeof(buf), "%s_g%d.so", name.c_str(), gen);
	return (dir / buf).string();
}

/* ---------------------------------------------------------------------------
 * Symbol resolution. resolve() itself does NOT take the mutex: it is called
 * from hookFromMod while the caller already holds it.
 * ------------------------------------------------------------------------- */

void *ModManager::resolve(const std::string &modName,
                          const std::string &symbol) const {
	if (symbol.empty()) return nullptr;

	if (!modName.empty()) {
		const ModRecord *rec = find(modName);
		if (!rec || !rec->handle)
			throw std::runtime_error("mod: no loaded mod named '" + modName + "'");
		dlerror();
		void *s = dlsym(rec->handle, symbol.c_str());
		if (dlerror() || !s)
			throw std::runtime_error("mod '" + modName + "': symbol not found: " + symbol);
		return s;
	}

	/* Mod-owned symbols resolve through the mod's own handle FIRST, and the
	 * global namespace LAST. This order is not stylistic: a
	 * dlsym(RTLD_DEFAULT) that lands in an RTLD_GLOBAL library registers a
	 * scope reference glibc's dlclose can never drop (measured: the library
	 * stays mapped and shadows later generations, so a hot reload's hook
	 * would patch the OLD text while its resolve() returned the NEW one).
	 * The sequence mod-handle -> RTLD_DEFAULT keeps generations closeable and
	 * keeps a mod's own exports from being shadowed by an earlier one. */
	if (installing_) {
		dlerror();
		void *s = dlsym(installing_->handle, symbol.c_str());
		if (dlerror() == nullptr && s) return s;
	}
	/* reverse load order: the most recently loaded generation wins */
	for (auto it = mods_.rbegin(); it != mods_.rend(); ++it) {
		if (!(*it)->handle) continue;
		dlerror();
		void *s = dlsym((*it)->handle, symbol.c_str());
		if (dlerror() == nullptr && s) return s;
	}

	/* host/global scope: symbols not owned by any mod (exe, libc, libs) */
	dlerror();
	void *s = dlsym(RTLD_DEFAULT, symbol.c_str());
	if (dlerror() == nullptr && s) return s;
	return nullptr;
}

/* ---------------------------------------------------------------------------
 * Hooks
 * ------------------------------------------------------------------------- */

void *ModManager::hookFromMod(const std::string &symbol, void *replacement) {
	std::lock_guard<std::recursive_mutex> lock(mutex_);
	const std::string owner = installing_ ? installing_->name : "";
	for (const auto &h : hooks_) {
		if (h.symbol == symbol)
			throw std::runtime_error("hook '" + symbol + "' already installed (owner '" +
			                         (h.modName.empty() ? std::string("script") : h.modName) + "')");
	}
	void *target = resolve("", symbol);
	if (!target)
		throw std::runtime_error("hook '" + symbol + "': symbol not resolvable (global scope or a loaded mod)");

	char err[HAVEL_MOD_ERR_SIZE];
	void *tramp = nullptr;
	if (havel_hook_install(target, replacement, &tramp, err, sizeof err) != 0)
		throw std::runtime_error(std::string("hook '") + symbol + "' failed: " + err);

	HookInfo info;
	info.symbol = symbol;
	info.target = target;
	info.replacement = replacement;
	info.trampoline = tramp;
	info.modName = owner;
	hooks_.push_back(std::move(info));
	HAVEL_LOGF_INFO("mod hook: '%s' at %p -> %p (trampoline %p, owner '%s')",
	                symbol.c_str(), target, replacement, tramp, owner.c_str());
	return tramp;
}

void *ModManager::hook(const std::string &symbol, void *replacement,
                       const std::string &ownerMod) {
	/* A script-installed hook carries the explicit owner name "". To reuse
	 * hookFromMod's single code path, attribute through installing_ rather
	 * than duplicating the install sequence. */
	std::lock_guard<std::recursive_mutex> lock(mutex_);
	ModRecord *saved = installing_;
	ModRecord *asOwner = ownerMod.empty() ? nullptr : find(ownerMod);
	if (!ownerMod.empty() && !asOwner)
		throw std::runtime_error("mod.hook: no loaded mod named '" + ownerMod + "' to attribute ownership to");
	installing_ = asOwner;
	try {
		void *out = hookFromMod(symbol, replacement);
		installing_ = saved;
		return out;
	} catch (...) {
		installing_ = saved;
		throw;
	}
}

bool ModManager::unhook(const std::string &symbol) {
	std::lock_guard<std::recursive_mutex> lock(mutex_);
	for (auto it = hooks_.begin(); it != hooks_.end(); ++it) {
		if (it->symbol != symbol) continue;
		char err[HAVEL_MOD_ERR_SIZE];
		int rc = havel_hook_remove(it->target, err, sizeof err);
		if (rc != 0)
			HAVEL_LOGF_ERROR("mod hook: unhook '%s' failed: %s", symbol.c_str(), err);
		hooks_.erase(it);
		return rc == 0;
	}
	return false;
}

std::vector<HookInfo> ModManager::hooks() const {
	std::lock_guard<std::recursive_mutex> lock(mutex_);
	return hooks_;
}

void ModManager::removeModHooks(const std::string &ownerName) {
	/* caller holds the mutex */
	for (auto it = hooks_.begin(); it != hooks_.end();) {
		if (it->modName != ownerName) { ++it; continue; }
		char err[HAVEL_MOD_ERR_SIZE];
		int rc = havel_hook_remove(it->target, err, sizeof err);
		if (rc != 0)
			HAVEL_LOGF_ERROR("mod hook: removing '%s' from unloaded mod '%s' failed: %s",
			                 it->symbol.c_str(), ownerName.c_str(), err);
		it = hooks_.erase(it);
	}
}

std::vector<ModInfo> ModManager::list() const {
	std::lock_guard<std::recursive_mutex> lock(mutex_);
	std::vector<ModInfo> out;
	out.reserve(mods_.size());
	for (const auto &m : mods_) {
		ModInfo info;
		info.name = m->name;
		info.path = m->path;
		info.loadedPath = m->loadedPath;
		info.generation = m->generation;
		out.push_back(std::move(info));
	}
	return out;
}

/* ---------------------------------------------------------------------------
 * Lifecycle. Each generation is a private copy of the .so under the temp
 * dir, so dlopen can never hand back a cached instance of stale text and an
 * in-place rewrite of the source file cannot corrupt the mapping already
 * loaded (a rewrite of a mmap'd file changes the code under the running
 * process - a known modding hazard).
 * ------------------------------------------------------------------------- */

void ModManager::resolveEntryPoints(ModRecord &rec) {
	dlerror();
	rec.init = (int (*)(HavelModApi *))dlsym(rec.handle, "havel_mod_init");
	if (!rec.init || dlerror())
		throw std::runtime_error("mod '" + rec.name + "': missing havel_mod_init");

	dlerror();
	rec.unloadFn = (void (*)())dlsym(rec.handle, "havel_mod_unload");
	if (dlerror()) rec.unloadFn = nullptr; /* optional */

	dlerror();
	rec.reloadFn = (int (*)(const void *, size_t))dlsym(rec.handle, "havel_mod_reload");
	if (dlerror()) rec.reloadFn = nullptr; /* optional */

	dlerror();
	rec.stateSizeFn = (size_t (*)())dlsym(rec.handle, "havel_mod_state_size");
	if (dlerror()) rec.stateSizeFn = nullptr; /* optional */

	dlerror();
	rec.stateSaveFn = (void (*)(void *))dlsym(rec.handle, "havel_mod_state_save");
	if (dlerror()) rec.stateSaveFn = nullptr; /* optional */
}

ModInfo ModManager::load(const std::string &path, bool globalScope) {
	std::lock_guard<std::recursive_mutex> lock(mutex_);

	struct stat st;
	if (stat(path.c_str(), &st) != 0)
		throw std::runtime_error(std::string("mod.load: cannot read '") + path + "': " + strerror(errno));

	fs::path src(path);
	std::string name = src.stem().string();
	if (name.size() > 3 && name.compare(0, 3, "lib") == 0) name = name.substr(3);
	if (name.empty())
		throw std::runtime_error("mod.load: cannot derive a mod name from '" + path + "'");
	for (char c : name) {
		if (!(std::isalnum((unsigned char)c) || c == '_' || c == '-'))
			throw std::runtime_error(std::string("mod.load: name '") + name +
			                         "' has a character the temp path cannot carry");
	}
	if (find(name))
		throw std::runtime_error("mod.load: '" + name + "' is already loaded (unload or reload it)");

	std::string genPath = generationPath(name, 1);
	std::error_code ec;
	fs::copy_file(path, genPath, fs::copy_options::overwrite_existing, ec);
	if (ec)
		throw std::runtime_error("mod.load: cannot stage " + path + " -> " + genPath + ": " + ec.message());

	void *h = dlopen(genPath.c_str(), RTLD_NOW | (globalScope ? RTLD_GLOBAL : RTLD_LOCAL));
	if (!h) {
		const char *derr = dlerror();
		fs::remove(genPath);
		throw std::runtime_error(std::string("mod.load: dlopen failed for ") + genPath + ": " +
		                         (derr ? derr : "unknown error"));
	}

	auto rec = std::make_unique<ModRecord>();
	rec->name = name;
	rec->path = fs::absolute(src).string();
	rec->loadedPath = genPath;
	rec->generation = 1;
	rec->globalScope = globalScope;
	rec->handle = h;
	try {
		resolveEntryPoints(*rec);
	} catch (...) {
		dlclose(h);
		fs::remove(genPath);
		throw;
	}

	/* init in the STABLE record, with installing_ naming it so a hook the mod
	 * installs during init is attributed to it (mods_ owns it now) */
	mods_.push_back(std::move(rec));
	installing_ = find(name);
	int rc = 0;
	try {
		rc = installing_->init(&api_);
	} catch (...) {
		installing_ = nullptr;
		removeModHooks(name);
		dlclose(h);
		fs::remove(genPath);
		for (auto it = mods_.begin(); it != mods_.end(); ++it)
			if ((*it)->name == name) { mods_.erase(it); break; }
		throw;
	}
	installing_ = nullptr;

	if (rc != 0) {
		/* init partially ran: remove what it installed, close it, forget it */
		removeModHooks(name);
		dlclose(h);
		fs::remove(genPath);
		for (auto it = mods_.begin(); it != mods_.end(); ++it)
			if ((*it)->name == name) { mods_.erase(it); break; }
		throw std::runtime_error("mod '" + name + "': havel_mod_init returned " + std::to_string(rc));
	}

	ModInfo info{name, fs::absolute(src).string(), genPath, 1};
	HAVEL_LOGF_INFO("mod: loaded '%s' from %s (as %s)", name.c_str(), path.c_str(), genPath.c_str());
	return info;
}

ModInfo ModManager::reload(const std::string &name) {
	std::lock_guard<std::recursive_mutex> lock(mutex_);

	ModRecord *rec = find(name);
	if (!rec)
		throw std::runtime_error("mod.reload: no loaded mod named '" + name + "'");

	/* 1. state out of the CURRENT instance */
	std::vector<uint8_t> state;
	if (rec->stateSizeFn && rec->stateSaveFn) {
		size_t n = rec->stateSizeFn();
		if (n) {
			state.resize(n);
			rec->stateSaveFn(state.data());
		}
	}

	/* 2. this mod's hooks point into the text about to disappear */
	removeModHooks(name);

	/* 3. unload the current generation */
	if (rec->unloadFn) rec->unloadFn();
	dlclose(rec->handle);
	std::error_code ec;
	fs::remove(rec->loadedPath, ec);

	/* 4. stage and open the fresh generation */
	int gen = rec->generation + 1;
	std::string genPath = generationPath(name, gen);
	fs::copy_file(rec->path, genPath, fs::copy_options::overwrite_existing, ec);
	if (ec)
		throw std::runtime_error("mod.reload: cannot stage " + rec->path + " -> " + genPath + ": " + ec.message());
	void *h = dlopen(genPath.c_str(), RTLD_NOW | (rec->globalScope ? RTLD_GLOBAL : RTLD_LOCAL));
	if (!h) {
		const char *derr = dlerror();
		fs::remove(genPath);
		throw std::runtime_error(std::string("mod.reload: dlopen failed for ") + genPath + ": " +
		                         (derr ? derr : "unknown error"));
	}

	/* 5. swap the generation into the record, init, restore state.
	 * The old generation is already closed by this point, so any failure
	 * here leaves the mod unusable: drop the record too, so mod.list() and
	 * a subsequent load() tell the truth instead of reporting a dead mod. */
	try {
		rec->handle = h;
		rec->loadedPath = genPath;
		rec->generation = gen;
		rec->init = nullptr;
		rec->unloadFn = nullptr;
		rec->reloadFn = nullptr;
		rec->stateSizeFn = nullptr;
		rec->stateSaveFn = nullptr;
		resolveEntryPoints(*rec);

		installing_ = rec;
		int rc = rec->init(&api_);
		installing_ = nullptr;
		if (rc != 0)
			throw std::runtime_error("mod '" + name + "': havel_mod_init returned " +
			                         std::to_string(rc) + " after reload");
		if (rec->reloadFn && !state.empty()) {
			int rrc = rec->reloadFn(state.data(), state.size());
			if (rrc != 0)
				HAVEL_LOGF_ERROR("mod '%s': havel_mod_reload returned %d; new instance starts with default state",
				                 name.c_str(), rrc);
		}
	} catch (...) {
		removeModHooks(name);
		dlclose(h);
		fs::remove(genPath);
		for (auto it = mods_.begin(); it != mods_.end(); ++it)
			if ((*it)->name == name) { mods_.erase(it); break; }
		throw;
	}

	HAVEL_LOGF_INFO("mod: reloaded '%s' as generation %d (%s)", name.c_str(), gen, genPath.c_str());
	ModInfo info{name, rec->path, genPath, gen};
	return info;
}

bool ModManager::unload(const std::string &name) {
	std::lock_guard<std::recursive_mutex> lock(mutex_);

	ModRecord *rec = find(name);
	if (!rec) return false;

	/* hooks this mod installed go first: their trampolines execute inside
	 * the call mapping that dlclose is about to tear down */
	removeModHooks(name);
	if (rec->unloadFn) rec->unloadFn();
	dlclose(rec->handle);
	std::error_code ec;
	fs::remove(rec->loadedPath, ec);

	for (auto it = mods_.begin(); it != mods_.end(); ++it) {
		if ((*it)->name == name) {
			mods_.erase(it);
			break;
		}
	}
	HAVEL_LOGF_INFO("mod: unloaded '%s'", name.c_str());
	return true;
}

} // namespace havel::modding