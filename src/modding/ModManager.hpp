#pragma once

/*
 * ModManager.hpp - lifecycle for Havel .so mods and their hooks.
 *
 * A mod is a shared library exporting the HavelModApi protocol from
 * include/havel/mod.h. The manager owns:
 *   - load/unload, with the file copied to a per-generation private path
 *     before dlopen (a fresh inode per generation, so dlopen can never
 *     return a cached instance of stale text, and an in-place rewrite by
 *     the modder cannot corrupt the mapping already loaded);
 *   - hot reload, which round-trips the mod's state blob through the
 *     protocol's optional state_size/state_save/state_reload hooks and
 *     removes the old generation's hooks BEFORE dlclose;
 *   - the hook table (target symbol -> trampoline), auto-cleaned when the
 *     owning mod unloads.
 *
 * Not thread-safe across hook install and unload of the same mod: mods
 * install hooks at init, and init/unload are single-threaded (the VM thread
 * drives them). The internal mutex covers bookkeeping only.
 */

#include <havel/mod.h>

#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

namespace havel::modding {

struct ModInfo {
	std::string name;
	std::string path;        /* original source path the script named */
	std::string loadedPath;  /* generation copy actually dlopen'd */
	int generation = 0;
};

struct HookInfo {
	std::string symbol;
	void *target = nullptr;
	void *replacement = nullptr;
	void *trampoline = nullptr;
	std::string modName;     /* owning mod, or "" for script-installed */
};

class ModManager {
public:
	static ModManager &instance();

	ModInfo load(const std::string &path, bool globalScope);
	ModInfo reload(const std::string &name);
	bool unload(const std::string &name);
	std::vector<ModInfo> list() const;

	/* Resolve a symbol. Empty modName: RTLD_DEFAULT first, then every
	 * loaded mod (a mod loaded with RTLD_GLOBAL is in the global scope, so
	 * RTLD_DEFAULT usually finds it; the extra search covers RTLD_LOCAL). */
	void *resolve(const std::string &modName, const std::string &symbol) const;

	/* Install a detour on `symbol`, resolved through resolve(). Returns the
	 * trampoline pointer (callable address of the original). Throws with the
	 * engine's precise message on failure. */
	void *hook(const std::string &symbol, void *replacement, const std::string &ownerMod);
	bool unhook(const std::string &symbol);
	std::vector<HookInfo> hooks() const;

	/* Internal: called from the HavelModApi thunks during mod init/reload so
	 * the hook gets attributed to the mod being installed. Throws on failure. */
	void *hookFromMod(const std::string &symbol, void *replacement);

private:
	ModManager();

	struct ModRecord {
		std::string name;
		std::string path;
		std::string loadedPath;
		int generation = 0;
		bool globalScope = true;
		void *handle = nullptr;
		int (*init)(HavelModApi *) = nullptr;
		void (*unloadFn)() = nullptr;
		int (*reloadFn)(const void *, size_t) = nullptr;
		size_t (*stateSizeFn)() = nullptr;
		void (*stateSaveFn)(void *) = nullptr;
	};

	/* Set while a mod's init() runs, so a hook it installs attributes to it. */
	ModRecord *installing_ = nullptr;

	ModRecord *find(const std::string &name);
	const ModRecord *find(const std::string &name) const;
	void removeModHooks(const std::string &ownerName);
	std::string generationPath(const std::string &name, int generation) const;
	/* Resolve the protocol's optional entry points on a freshly dlopen'd
	 * generation. dlsym leaves errno unset for a missing symbol, so the
	 * optional ones are simply absent. */
	void resolveEntryPoints(ModRecord &rec);

	/* unique_ptr so ModRecord addresses are stable across load/unload: a mod's
	 * init can load another mod, and installing_ (used to attribute hooks to
	 * the mod whose init is running) must survive that. */
	std::vector<std::unique_ptr<ModRecord>> mods_;
	std::vector<HookInfo> hooks_;
	mutable std::recursive_mutex mutex_;
	HavelModApi api_{};
};

} // namespace havel::modding