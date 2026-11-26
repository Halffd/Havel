# hpkg

Havel's package manager: publishes, installs, and version-resolves Havel
script packages. npm/cargo-shaped, filesystem-backed — the registry is a
directory, so nothing needs a server.

## Layout

```text
~/.havel/registry/<name>/<version>/   published package contents
~/.havel/packages/<name>/             installed package
~/.havel/havel.lock                   pin: name <TAB> version <TAB> source <TAB> spec
```

`HAVEL_REGISTRY_DIR` redirects the registry (used by the test suite to keep
scratch registries off your real one). Installed packages are what the module
loader resolves bare `use <name>` imports against (see
[modding/mods.md](../modding/mods.md) for the loader side).

## Package manifest

`hpkg init` scaffolds `havel.toml` plus `<name>.hv`:

```toml
[package]
name = "my-pkg"
version = "0.1.0"
description = "what it does"
license = "MIT"

[dependencies]
other = "^1.2.0"
```

## Commands

| Command | Effect |
|---|---|
| `hpkg init [name]` | scaffold a package in the current directory |
| `hpkg publish` | copy the package into `~/.havel/registry/<name>/<version>`; refuses to overwrite a published version |
| `hpkg publish --local` | install the current directory into `~/.havel/packages` (dependencies resolved from the registry) |
| `hpkg install <path>` | install from a directory with `havel.toml` |
| `hpkg install <name>` | resolve the best registry version and install it, dependencies first |
| `hpkg install <name>@<spec>` | resolve a specific constraint |
| `hpkg install <file>.hv` | install a single file as `~/.havel/packages/<stem>/<stem>.hv` |
| `hpkg list` | installed packages |
| `hpkg info [name]` | manifest of the current directory or a named package |
| `hpkg remove <name>` | remove; refuses while installed packages depend on it (`--force` overrides) |
| `hpkg update [name...]` | re-resolve locked packages against their recorded specs |
| `hpkg tree [name...]` | dependency tree of installed packages |

Toolchain commands (`hpkg toolchain ...`) are separate: per-commit `havel`
binaries for regression testing, stored under `~/.havel/toolchains/`.

## Version constraints

Same shapes as npm/cargo:

| Spec | Meaning |
|---|---|
| `1.2.3` | exactly 1.2.3 |
| `*` | any version (default when omitted) |
| `^1.2.3` | `>=1.2.3`, `<2.0.0` — while major is 0, `<0.3.0`/`<0.0.4` (minor/patch is the breaking axis) |
| `~1.2.3` | `>=1.2.3`, `<1.3.0` |
| `>=1.2.3`, `<=2.0.0`, `>1.0`, `<2` | comparators |
| `>=1.2.3, <2.0.0` | comma-joined, all must hold |

`||` (or) is rejected rather than half-supported. Resolution picks the
highest published version satisfying the constraint; the resolved set is
pinned in `havel.lock`, and `update` re-resolves against the lock's recorded
spec — so a `~1.1.0` dependency never crosses into 2.0.0 on update.

## Tests

```bash
HAVEL_STDLIB=modules/std build-debug/hvtest --smoke hpkg
```

`test_hpkg_package_modules` proves installed packages resolve as bare module
imports; `test_hpkg_registry` exercises publish, constraint resolution,
recursive installs, the lockfile, uninstall dependents, update, and tree
against a scratch registry and HOME.

## Not yet implemented

Remote registries: `publish` writes to the local registry only. Dependency
resolution, semver, lockfile, and dependency-aware uninstall all run locally.
The lockfile's `source` field and the registry's directory layout are chosen
so a network registry can slot in without changing the file formats.