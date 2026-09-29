#!/usr/bin/env python3
"""
gen_host_function_names.py - generate the canonical host-function name list.

Rust-like source of truth: a literal host-function registration call IS the
declaration of a script-callable name. This script scans every such literal
across src/ and emits HostFunctionNames.generated.hpp containing
kHostFunctionNames[].

Registration forms recognized (all with literal string names; dynamic name
construction is intentionally out of scope):

    registerHostFunction("name", ...)      VM bootstrap + VM internals
    host_functions["name"]                 bridge installers
    registerFunction("name", ...)          stdlib/plugin modules (VMApi)
    api.registerFunction("name", ...)      same, explicit receiver

The strict-mode resolver in HavelLauncher::runBuild (a compile-time-only path
with no VM instance) derives its known globals from this generated list, so a
runtime registration can never silently go unknown to --build/--strict.
Drift is structurally impossible: both share the generated list; a stale
checked-in copy is caught by the freshness drift check
(scripts/check_havel_launcher_globals.sh).

Dotted names are kept whole (the registered name) and also reduced to their
first segment (the object global), mirroring the pipeline's seeding.
Double-underscore names are internal seams, not script-visible names.

Usage:
    gen_host_function_names.py SRC_DIR OUTPUT_HPP
"""

import argparse
import os
import re
import sys

# registerHostFunction("name", / ) - the closing quote must be followed by a
# comma or paren, which excludes dynamically composed registrations such as
# registerHostFunction("array." + method, ...) (their names are built at
# runtime from method lists, so the bare prefix is not itself a name).
REGISTER_HOST_RE = re.compile(
    r'registerHostFunction\(\s*"([A-Za-z_][A-Za-z0-9_.]*)"\s*[,)]'
)
HOST_FUNCTIONS_RE = re.compile(r'host_functions\[\s*"([A-Za-z_][A-Za-z0-9_.]*)"\s*\]')
REGISTER_FN_RE = re.compile(r'registerFunction\(\s*"([A-Za-z_][A-Za-z0-9_.]*)"\s*[,)]')

# Files that are outputs of this generator (or otherwise contain the
# registration pattern only in prose/comments) are excluded from the scan so
# the generator cannot self-pollute the list.
SKIP_FILES = frozenset(
    {
        "HostFunctionNames.generated.hpp",
        "ModuleGlobals.generated.hpp",
        "ModuleGlobals.hpp",
    }
)

# Internal seams and scratch names that must not surface as script-visible
# names. Double-underscore names are test seams (__async_probe) or parser
# stubs (__get_input_stub__): never user code, never typo targets.
INTERNAL_PREFIXES = (
    "__",
    "_G",
    "$",
    "0",
    "1",
    "2",
    "3",
    "4",
    "5",
    "6",
    "7",
    "8",
    "9",
)


def is_internal(name: str) -> bool:
    return name.startswith(INTERNAL_PREFIXES)


def collect_names(src_dir: str):
    names = set()
    for root, _dirs, files in os.walk(src_dir):
        for fn in files:
            if not (fn.endswith(".cpp") or fn.endswith(".hpp") or fn.endswith(".h")):
                continue
            if fn in SKIP_FILES:
                continue
            path = os.path.join(root, fn)
            try:
                with open(path, "r", encoding="utf-8", errors="replace") as f:
                    text = f.read()
            except OSError:
                continue
            for regex in (REGISTER_HOST_RE, HOST_FUNCTIONS_RE, REGISTER_FN_RE):
                for m in regex.finditer(text):
                    full = m.group(1)
                    if is_internal(full):
                        continue
                    names.add(full)
                    first = full.split(".", 1)[0]
                    if not is_internal(first):
                        names.add(first)
    return names


def emit_header(names, header_guard):
    lines = []
    lines.append("// GENERATED FILE - DO NOT EDIT.")
    lines.append("//")
    lines.append("// Produced by scripts/gen_host_function_names.py from the actual")
    lines.append("// host-function registration sites across src/. The strict-mode")
    lines.append("// resolver in HavelLauncher::runBuild derives its known globals")
    lines.append("// from this list, so a runtime registration can never silently go")
    lines.append("// unknown to --build. Rebuild this file by re-running the")
    lines.append("// generator (wired as a pre-build step); staleness is caught by")
    lines.append("// scripts/check_havel_launcher_globals.sh.")
    lines.append("#pragma once")
    lines.append("")
    lines.append(f"#ifndef {header_guard}")
    lines.append(f"#define {header_guard}")
    lines.append("")
    lines.append("namespace havel::compiler {")
    lines.append("")
    lines.append("inline constexpr const char *kHostFunctionNames[] = {")
    for n in sorted(names):
        lines.append(f'    "{n}",')
    lines.append("};")
    lines.append("")
    lines.append("} // namespace havel::compiler")
    lines.append("")
    lines.append(f"#endif // {header_guard}")
    lines.append("")
    return "\n".join(lines)


def main(argv):
    parser = argparse.ArgumentParser(
        description="Generate the canonical host-function name header."
    )
    parser.add_argument("src_dir", help="source directory to scan (recursive)")
    parser.add_argument("output", help="output header path")
    args = parser.parse_args(argv)

    src_dir = os.path.abspath(args.src_dir)
    if not os.path.isdir(src_dir):
        print(f"ERROR: source dir not found: {src_dir}", file=sys.stderr)
        return 1

    names = collect_names(src_dir)

    guard = "HAVEL_COMPILER_CORE_HOSTFUNCTIONNAMES_GENERATED_HPP"
    header = emit_header(names, guard)

    output = os.path.abspath(args.output)
    out_dir = os.path.dirname(output)
    if out_dir:
        os.makedirs(out_dir, exist_ok=True)

    try:
        with open(output, "w", encoding="utf-8") as f:
            f.write(header)
    except OSError as e:
        print(f"ERROR: cannot write {output}: {e}", file=sys.stderr)
        return 1

    print(f"Generated {output} with {len(names)} names.")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
