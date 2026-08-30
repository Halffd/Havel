#!/usr/bin/env python3
"""
gen_module_globals.py - generate the canonical module-global name list.

Rust-like source of truth: the literal `setGlobal("name", ...)` call in a
module or VM host bridge IS the declaration of a script-visible global. This
script scans every such literal across src/, strips the VM-core language
builtins (which runBuild resolves via its keyword/builtin list, not the module
list), and emits ModuleGlobals.generated.hpp containing kModuleGlobals[].

Drift is structurally impossible: the compiler's strict-mode known-globals set
is derived from the very setGlobal() declarations it must accept, so adding a
setGlobal("x") can never silently leave x unknown to --build/--full-aot.

Usage:
    gen_module_globals.py SRC_DIR OUTPUT_HPP

The SRC_DIR is searched recursively for *.cpp / *.hpp files. Every literal
setGlobal("name", ...) is collected. Dotted member names (thread.spawn, ...)
are reduced to their first segment (the actual global). VM-core language
builtins (see VM_CORE_BUILTINS) are excluded: they are language-level type /
constructor / numeric-constant objects, not module globals.

The generated header is included by ModuleGlobals.hpp and consumed by the
strict-mode resolver in Pipeline.cpp and HavelLauncher.cpp.
"""

import argparse
import os
import re
import sys

# VM-core language builtins set via setGlobal() by the VM core itself, not by
# modules. Strict-mode resolution supplies these from its own keyword/built-in
# lists, so they are intentionally excluded from the module-global list. This
# set is small and stable (language-level types/constructors/constants); it is
# NOT the drift-prone module list.
VM_CORE_BUILTINS = frozenset(
    {
        "class",
        "Class",
        "struct",
        "Struct",
        "prot",
        "load",
        "scheduler",
        "None",
        "Object",
        "object",
        "Option",
        "newEnum",
        "getVariant",
        "E",
        "INF",
        "NAN",
        "PI",
        "RAND_MAX",
    }
)

# setGlobal("name" is the only recognized literal form. Dynamic name
# construction (setGlobal(name, ...) with a runtime string) is not a
# script-visible global declaration and is intentionally out of scope here.
SETGLOBAL_RE = re.compile(r'setGlobal\(\s*"([A-Za-z_][A-Za-z0-9_.]*)"')

# Files that are scaffolding/outputs of this generator (or otherwise contain
# the setGlobal("...") pattern only in prose/comments, not as real runtime
# declarations) are excluded from the scan. Without this, the generator would
# pick up the literal backticks in its own header text and self-pollute the
# list.
SKIP_FILES = frozenset(
    {
        "ModuleGlobals.hpp",
        "ModuleGlobals.generated.hpp",
    }
)

# Runtime-internal globals that must not surface as script-visible module
# globals (scratch/generated names).
INTERNAL_PREFIXES = (
    "_G",
    "__export_",
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
    return name in VM_CORE_BUILTINS or name.startswith(INTERNAL_PREFIXES)


def collect_setglobal_names(src_dir: str):
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
            for m in SETGLOBAL_RE.finditer(text):
                full = m.group(1)
                first = full.split(".", 1)[0]
                if is_internal(first):
                    continue
                names.add(first)
    return names


def emit_header(names, header_guard):
    lines = []
    lines.append("// GENERATED FILE - DO NOT EDIT.")
    lines.append("//")
    lines.append("// Produced by scripts/gen_module_globals.py from the actual")
    lines.append('// setGlobal("name") declarations across src/. The strict-mode')
    lines.append("// resolver (Pipeline.cpp, HavelLauncher.cpp) derives its known")
    lines.append("// module globals from this list, so a runtime setGlobal() can")
    lines.append("// never silently go unknown to --build/--full-aot. Rebuild this")
    lines.append("// file by re-running the generator (wired as a pre-build step).")
    lines.append("#pragma once")
    lines.append("")
    lines.append(f"#ifndef {header_guard}")
    lines.append(f"#define {header_guard}")
    lines.append("")
    lines.append("namespace havel::compiler {")
    lines.append("")
    lines.append("inline constexpr const char *kModuleGlobals[] = {")
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
        description="Generate the canonical module-global name header."
    )
    parser.add_argument("src_dir", help="source directory to scan (recursive)")
    parser.add_argument("output", help="output header path")
    args = parser.parse_args(argv)

    src_dir = os.path.abspath(args.src_dir)
    if not os.path.isdir(src_dir):
        print(f"ERROR: source dir not found: {src_dir}", file=sys.stderr)
        return 1

    names = collect_setglobal_names(src_dir)

    guard = "HAVEL_COMPILER_CORE_MODULEGLOBALS_GENERATED_HPP"
    header = emit_header(names, guard)

    output = os.path.abspath(args.output)
    out_dir = os.path.dirname(output)
    if out_dir:
        os.makedirs(out_dir, exist_ok=True)

    # Write only if changed to avoid needless rebuild churn.
    try:
        with open(output, "r", encoding="utf-8") as f:
            if f.read() == header:
                print(f"ModuleGlobals.generated.hpp: unchanged ({len(names)} names)")
                return 0
    except OSError:
        pass

    with open(output, "w", encoding="utf-8") as f:
        f.write(header)
    print(f"ModuleGlobals.generated.hpp: wrote {len(names)} names")

    # Verify a few representative module globals actually emitted (sanity).
    # Host functions (print, sleep, ...) are registered via
    # registerHostFunction, NOT setGlobal, and are intentionally absent here.
    missing = {"system", "sys", "fs", "shell", "math", "string"} - names
    if missing:
        print(
            f"WARNING: expected module globals missing from scan: {sorted(missing)}",
            file=sys.stderr,
        )
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
