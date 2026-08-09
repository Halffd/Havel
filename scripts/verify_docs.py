#!/usr/bin/env python3
"""
Verify that all modules and functions from the actual implementation
are documented in the docs/ directory.
"""

import os
import re
import subprocess

# 1. Extract C++ module functions
def get_cpp_module_functions():
    cpp_modules = {}
    stdlib_path = "/home/all/repos/havel-3/src/havel-lang/stdlib"
    for f in os.listdir(stdlib_path):
        if f.endswith("Module.cpp"):
            module_name = f.replace("Module.cpp", "").lower()
            cpp_modules[module_name] = set()
            with open(os.path.join(stdlib_path, f), 'r') as fp:
                content = fp.read()
                # Find all registerFunction calls
                matches = re.findall(r'registerFunction\("([^"]+)"', content)
                for m in matches:
                    cpp_modules[module_name].add(m)
    return cpp_modules

# 2. Extract Havel sidecar module functions
def get_havel_module_functions():
    havel_modules = {}
    std_path = "/home/all/repos/havel-3/modules/std"
    for f in os.listdir(std_path):
        if f.endswith(".hv") and not f.startswith("_"):
            module_name = f.replace(".hv", "")
            havel_modules[module_name] = set()
            with open(os.path.join(std_path, f), 'r') as fp:
                content = fp.read()
                # Find all fn declarations
                matches = re.findall(r'^\s*fn\s+([a-zA-Z_][a-zA-Z0-9_]*)', content, re.MULTILINE)
                for m in matches:
                    if not m.startswith('_'):  # Skip private
                        havel_modules[module_name].add(m)
    return havel_modules

# 3. Extract app module functions
def get_app_module_functions():
    app_modules = {}
    app_path = "/home/all/repos/havel-3/modules/app"
    for f in os.listdir(app_path):
        if f.endswith(".hv") and not f.startswith("_"):
            module_name = f.replace(".hv", "")
            app_modules[module_name] = set()
            with open(os.path.join(app_path, f), 'r') as fp:
                content = fp.read()
                # Find all fn declarations in object methods
                matches = re.findall(r'^\s*fn\s+([a-zA-Z_][a-zA-Z0-9_]*)', content, re.MULTILINE)
                for m in matches:
                    if not m.startswith('_'):
                        app_modules[module_name].add(m)
    return app_modules

# 4. Parse docs
def get_documented_functions():
    documented = {}
    docs_stdlib = "/home/all/repos/havel-3/docs/stdlib"
    for f in os.listdir(docs_stdlib):
        if f.endswith(".md"):
            module_name = f.replace(".md", "")
            documented[module_name] = set()
            with open(os.path.join(docs_stdlib, f), 'r') as fp:
                content = fp.read()
                # Look for function patterns in code blocks and tables
                # Pattern: `module.function` or `function()`
                matches = re.findall(r'`([a-z_]+\.[a-z_]+)\([^`]*`', content)
                for m in matches:
                    documented[module_name].add(m)
                # Also look for standalone functions
                matches = re.findall(r'`([a-z_]+)\([^`]*`', content)
                for m in matches:
                    if '.' not in m:
                        documented[module_name].add(m)
    return documented

def main():
    print("=== C++ Module Functions (from ShellModule.cpp etc) ===")
    cpp_funcs = get_cpp_module_functions()
    for mod, funcs in sorted(cpp_funcs.items()):
        print(f"\n{mod} ({len(funcs)} functions):")
        for f in sorted(funcs):
            print(f"  {f}")

    print("\n\n=== Havel stdlib Modules (from modules/std/) ===")
    havel_funcs = get_havel_module_functions()
    for mod, funcs in sorted(havel_funcs.items()):
        print(f"\n{mod} ({len(funcs)} functions):")
        for f in sorted(funcs):
            print(f"  {f}")

    print("\n\n=== App Modules (from modules/app/) ===")
    app_funcs = get_app_module_functions()
    for mod, funcs in sorted(app_funcs.items()):
        print(f"\n{mod} ({len(funcs)} functions):")
        for f in sorted(funcs):
            print(f"  {f}")

    print("\n\n=== Documented Functions ===")
    doc_funcs = get_documented_functions()
    for mod, funcs in sorted(doc_funcs.items()):
        print(f"\n{mod} ({len(funcs)} functions):")
        for f in sorted(funcs):
            print(f"  {f}")

    # Check missing
    print("\n\n=== MISSING FROM DOCS ===")
    
    # Check C++ modules vs docs
    doc_mod_names = set(doc_funcs.keys())
    cpp_mod_names = set(cpp_funcs.keys())
    
    # Map C++ module names to doc names
    cpp_to_doc = {
        'math': 'math',
        'string': 'string',
        'array': 'array',
        'object': 'object',
        'fs': 'fs',
        'shell': 'shell',  # was 'process'
        'sys': 'sys',
        'time': 'time',
        'log': 'log',  # may not have doc
        'http': 'network',
        'bit': 'bit',  # may not have doc
        'type': 'type',  # may not have doc
        'random': 'random',  # may not have doc
        'timer': 'timer',  # may not have doc
        'regex': 'regex',  # may not have doc
        'format': 'format',  # may not have doc
        'pack': 'pack',  # may not have doc
        'pointer': 'pointer',  # may not have doc
        'state': 'state',  # may not have doc
        'option': 'option',  # may not have doc
        'readline': 'readline',  # may not have doc
        'crypto': 'crypto',  # may not have doc
        'process': 'shell',  # maps to shell
    }
    
    for cpp_mod, doc_mod in cpp_to_doc.items():
        if cpp_mod in cpp_funcs and doc_mod in doc_funcs:
            cpp_set = cpp_funcs[cpp_mod]
            doc_set = doc_funcs[doc_mod]
            missing = cpp_set - doc_set
            if missing:
                print(f"\n{doc_mod} (from C++ {cpp_mod}): missing {len(missing)} functions:")
                for f in sorted(missing):
                    print(f"  {f}")

if __name__ == "__main__":
    main()
