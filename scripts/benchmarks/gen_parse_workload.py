#!/usr/bin/env python3
# Generates a large Havel source file for self-hosted-compiler parse
# benchmarking: many top-level function definitions with mixed
# expressions, calls, conditionals, loops and member access, so the
# parser's dispatch functions (pratt.nud/led, parseStatement, ...) run
# enough iterations to trip the tiering backedge threshold.
#
# Usage: gen_parse_workload.py [funcs] > out.hv
import sys

TARGET_FUNCS = int(sys.argv[1]) if len(sys.argv) > 1 else 120

out = []
out.append("// generated workload: parser throughput benchmark")
out.append("")
for i in range(TARGET_FUNCS):
    out.append(f"fn w{i}(a, b) {{")
    out.append("  local total = 0")
    out.append("  for k in 0 .. (a % 7 + 2) {{")
    out.append(
        "    if k > b {{ total = total + k * 2 - b }} else {{ total = total + k }}"
    )
    out.append("    total = total + (a * b) / (k + 1)")
    out.append("    total = total % (k + 3)")
    out.append("  }}")
    out.append(f"  local obj = {{ alpha: total, beta: total * 2, gamma: -total }}")
    out.append("  local parts = []")
    out.append("  for p in 0 .. 4 {{ parts.push(obj.alpha + p) }}")
    out.append("  return total + parts.len() + obj.beta + obj.gamma")
    out.append("}")
    out.append("")

out.append("fn main() {")
out.append("  local acc = 0")
for i in range(TARGET_FUNCS):
    out.append(f"  acc = acc + w{i}({i % 5 + 1}, {i % 3 + 1})")
out.append('  print("acc=${acc}")')
out.append("}")
out.append("")

print("\n".join(out))
