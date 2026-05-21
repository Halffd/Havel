modules_info = [
    ("ast", "modules/lang/ast.hv"),
    ("scope", "modules/lang/scope.hv"),
    ("lexer", "modules/lang/lexer.hv"),
    ("pratt", "modules/lang/pratt.hv"),
    ("emitter", "modules/lang/emitter.hv"),
]

compiler_main = "modules/compiler.hv"
bundle_module_names = {"ast", "scope", "lexer", "pratt", "emitter"}

def filter_uses(source, internal_names):
    """Remove use statements referencing internal bundled modules. Keep others."""
    lines = source.split('\n')
    result = []
    i = 0
    while i < len(lines):
        line = lines[i]
        stripped = line.strip()
        # Check if this line starts a 'use' statement with 'from' clause
        if stripped.startswith('use ') and ' from ' in stripped:
            # Single-line use: use { X } from "mod"
            is_internal = False
            for name in internal_names:
                if f'from "{name}"' in stripped or f"from '{name}'" in stripped:
                    is_internal = True
                    break
            if not is_internal:
                result.append(line)
            i += 1
            continue
        
        # Multi-line use: use { ... } \n from "mod"
        if stripped.startswith('use ') and ' from ' not in stripped:
            # Collect the full use statement
            use_lines = [line]
            j = i + 1
            full_text = stripped
            while j < len(lines):
                use_lines.append(lines[j])
                full_text += ' ' + lines[j].strip()
                if 'from' in lines[j]:
                    break
                # If we hit a line that starts with a non-use continuation, stop
                if lines[j].strip() and not lines[j].strip().startswith('{') and not lines[j].strip().startswith('}') and 'from' not in lines[j] and not lines[j].strip().startswith(','):
                    break
                j += 1
            
            is_internal = False
            for name in internal_names:
                if f'from "{name}"' in full_text or f"from '{name}'" in full_text:
                    is_internal = True
                    break
            
            if not is_internal:
                result.extend(use_lines)
            i = j + 1
            continue
        
        result.append(line)
        i += 1
    
    return '\n'.join(result)


output_parts = []

for name, path in modules_info:
    with open(path, 'r') as f:
        src = f.read()
    filtered = filter_uses(src, bundle_module_names)
    output_parts.append(f"// === BEGIN MODULE: {name} ===")
    output_parts.append(filtered)
    output_parts.append(f"// === END MODULE: {name} ===")
    output_parts.append("")

with open(compiler_main, 'r') as f:
    main_src = f.read()
filtered_main = filter_uses(main_src, bundle_module_names)
output_parts.append("// === BEGIN MODULE: compiler_main ===")
output_parts.append(filtered_main)
output_parts.append("// === END MODULE: compiler_main ===")

result = '\n'.join(output_parts)
with open("modules/compiler_bundle.hv", 'w') as f:
    f.write(result)
print(f"Written {len(result)} bytes ({result.count(chr(10))} lines) to modules/compiler_bundle.hv")

# Verify
remaining_uses = [l for l in result.split('\n') if l.strip().startswith('use ')]
print(f"Remaining use statements: {len(remaining_uses)}")
for u in remaining_uses:
    print(f"  {u.strip()[:80]}")

for name in bundle_module_names:
    # Only check in actual use statements, not comments
    in_uses = any(f'from "{name}"' in u or f"from '{name}'" in u for u in remaining_uses)
    if in_uses:
        print(f"WARNING: internal use from '{name}' still present!")
    else:
        print(f"OK: no use from '{name}'")
