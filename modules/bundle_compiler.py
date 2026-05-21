modules_info = [
    ("ast",       "modules/lang/ast.hv"),
    ("scope",     "modules/lang/scope.hv"),
    ("error",     "modules/lang/error.hv"),
    ("debug",     "modules/lang/debug.hv"),
    ("lexer",     "modules/lang/lexer.hv"),
    ("pratt",     "modules/lang/pratt.hv"),
    ("types",     "modules/lang/types.hv"),
    ("optimizer", "modules/lang/optimizer.hv"),
    ("emitter",   "modules/lang/emitter.hv"),
    ("launcher",  "modules/lang/launcher.hv"),
    ("libtest",   "modules/lang/libtest.hv"),
]

compiler_main = "modules/compiler.hv"

output_parts = []
for name, path in modules_info:
    with open(path, 'r') as f:
        src = f.read()
    output_parts.append(f"// === BEGIN MODULE: {name} ===")
    output_parts.append(src)
    output_parts.append(f"// === END MODULE: {name} ===")
    output_parts.append("")

with open(compiler_main, 'r') as f:
    main_src = f.read()
output_parts.append("// === BEGIN MODULE: compiler_main ===")
output_parts.append(main_src)
output_parts.append("// === END MODULE: compiler_main ===")

result = '\n'.join(output_parts)
with open("modules/compiler_bundle.hv", 'w') as f:
    f.write(result)
print(f"Written {len(result)} bytes ({result.count(chr(10))} lines) to modules/compiler_bundle.hv")

use_lines = [l for l in result.split('\n') if l.strip().startswith('use ')]
print(f"Use statements: {len(use_lines)}")
for u in use_lines:
    print(f"  {u.strip()[:100]}")
