// Host lifecycle test for the modding module: load a real demo mod, prove a
// detour it installs fires, hot-reload it and prove state survives while the
// instance is genuinely fresh, then unload and re-load.
//
// Built and run by scripts/ci/mod-lifecycle-test.sh. Links ModManager.cpp,
// hook.c and LoggerC.c from the tree against the freshly compiled demo mod.
#include "modding/ModManager.hpp"

#include <dlfcn.h>
#include <stdio.h>
#include <string.h>

using havel::modding::ModManager;
using havel::modding::ModInfo;

static int fails = 0;

static void check(const char *what, bool ok) {
    if (ok) {
        printf("PASS %s\n", what);
    } else {
        printf("FAIL %s\n", what);
        fails++;
    }
}

typedef int (*int_fn)(void);
typedef int (*add_fn)(int, int);

static void *sym(ModManager &mgr, const std::string &name, const char *sym) {
    return mgr.resolve(name, sym);
}

int main(int argc, char **argv) {
    setvbuf(stdout, NULL, _IONBF, 0);
    if (argc != 2) {
        fprintf(stderr, "usage: %s /path/to/libdemo_mod.so\n", argv[0]);
        return 2;
    }
    const std::string so = argv[1];
    auto &mgr = ModManager::instance();

    printf("== load ==\n");
    ModInfo info;
    try {
        info = mgr.load(so, true);
        check("load returns generation 1", info.generation == 1);
        check("load derives the name from the file", info.name == "demo_mod");
    } catch (const std::exception &e) {
        printf("FAIL load: %s\n", e.what());
        return 1;
    }

    int_fn counter = (int_fn)sym(mgr, info.name, "demo_counter");
    int_fn bump = (int_fn)sym(mgr, info.name, "demo_bump");
    int_fn instances = (int_fn)sym(mgr, info.name, "demo_instances");
    add_fn add = (add_fn)sym(mgr, info.name, "demo_add");
    check("resolves the mod's exports", counter && bump && instances && add);

    if (counter && bump) {
        check("counter starts at 0", counter() == 0);
        check("bump returns 1", bump() == 1);
        check("bump returns 2", bump() == 2);
    }
    if (add) check("detour installed by the mod fires: demo_add(2,3) == 1005", add(2, 3) == 1005);
    check("one hook is registered", mgr.hooks().size() == 1);
    if (!mgr.hooks().empty())
        check("the hook is attributed to the mod", mgr.hooks()[0].modName == "demo_mod");

    printf("== reload (hot reload with state) ==\n");
    ModInfo info2;
    int_fn counter2 = nullptr;
    int_fn instances2 = nullptr;
    add_fn add2 = nullptr;
    try {
        info2 = mgr.reload(info.name);
        check("reload returns generation 2", info2.generation == 2);
        check("reload uses a different file", info2.loadedPath != info.loadedPath);
        counter2 = (int_fn)sym(mgr, info2.name, "demo_counter");
        instances2 = (int_fn)sym(mgr, info2.name, "demo_instances");
        add2 = (add_fn)sym(mgr, info2.name, "demo_add");
    } catch (const std::exception &e) {
        printf("FAIL reload: %s\n", e.what());
        return 1;
    }
    if (counter2 && instances2 && add2) {
        check("state preserved across reload (counter == 2)", counter2() == 2);
        check("fresh instance really loaded (instances == 1)", instances2() == 1);
        check("new generation re-installed its hook: demo_add(2,3) == 1005", add2(2, 3) == 1005);
    }
    check("still exactly one hook after reload", mgr.hooks().size() == 1);
    /* The old generation must be GONE: RTLD_NOLOAD returns non-null only for
     * a still-mapped object. A stale generation left mapped means
     * resolve() found the old demo_add and the hook was just patched into
     * code that no longer runs - exactly the bug this guards. */
    {
        void *oldmapped = dlopen(info.loadedPath.c_str(), RTLD_LAZY | RTLD_NOLOAD);
        check("old generation is unmapped after hot reload", oldmapped == nullptr);
        if (oldmapped) dlclose(oldmapped);
        void *newmapped = dlopen(info2.loadedPath.c_str(), RTLD_LAZY | RTLD_NOLOAD);
        check("new generation is mapped", newmapped != nullptr);
        if (newmapped) dlclose(newmapped);
    }

    printf("== reload again (generation 3) ==\n");
    try {
        ModInfo info3 = mgr.reload(info.name);
        check("reload returns generation 3", info3.generation == 3);
        int_fn counter3 = (int_fn)sym(mgr, info3.name, "demo_counter");
        check("state preserved across the second reload", counter3 && counter3() == 2);
    } catch (const std::exception &e) {
        printf("FAIL second reload: %s\n", e.what());
        return 1;
    }

    printf("== unload / re-load ==\n");
    check("unload returns true", mgr.unload(info.name));
    check("list is empty after unload", mgr.list().empty());
    check("the mod's hook was removed on unload", mgr.hooks().empty());
    check("unloading an unknown mod returns false", mgr.unload("demo_mod") == false);
    try {
        ModInfo again = mgr.load(so, true);
        check("re-load returns generation 1", again.generation == 1);
        int_fn counter3 = (int_fn)sym(mgr, again.name, "demo_counter");
        check("re-load restarts state at 0", counter3 && counter3() == 0);
    } catch (const std::exception &e) {
        printf("FAIL re-load: %s\n", e.what());
        return 1;
    }

    if (fails) {
        printf("FAIL: %d check(s)\n", fails);
        return 1;
    }
    printf("PASS: mod lifecycle (load, hook, hot reload, state, unload)\n");
    return 0;
}