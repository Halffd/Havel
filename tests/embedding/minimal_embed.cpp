// minimal_embed.cpp — the architectural invariant test.
//
// Links ONLY libhavel_lang (runtime + compiler) and libhavel_core. If a Qt
// symbol ever re-enters those archives, this target fails to link (the
// executable deliberately does not link Qt), and
// scripts/ci/embedding-qt-audit.sh fails on the Qt-mangled symbols in the
// archives. See the embedding-qt-invariant ctest entry.

#include "havel-lang/capi/havel.h"
#include "havel-lang/compiler/core/SourceModuleCompilerHook.hpp"

#include <cstdio>
#include <cstring>

static int failures = 0;

static void check(int cond, const char *name) {
  std::printf("%s: %s\n", cond ? "PASS" : "FAIL", name);
  if (!cond)
    failures++;
}

int main() {
  // SDK hosts install the parse+compile pipeline at startup; without it the
  // runtime answers every loadstring with "no compiler available".
  havel::compiler::registerSourceModuleCompilerHook();

  HavelState *H = havel_newstate();
  if (!H) {
    std::printf("FAIL: havel_newstate returned null\n");
    return 1;
  }
  check(H != nullptr, "havel_newstate");

  // Pure-language arithmetic: compiler + VM, no host services.
  int rc = havel_loadstring(H, "return 40 + 2", "embed_arith");
  if (rc != HAVEL_OK) {
    std::printf("  loadstring error: %s\n", havel_lasterror(H));
  }
  check(rc == HAVEL_OK, "loadstring arithmetic");
  if (rc == HAVEL_OK) {
    int top = havel_gettop(H);
    // The C API stack is 0-based: the last pushed value sits at top-1.
    double v = havel_tonumber(H, top - 1);
    check(v == 42.0, "arithmetic result 42");
  }

  // Runtime stdlib reachable from an embedding: string method call.
  rc = havel_loadstring(H, "return \"hello\".upper()", "embed_string");
  check(rc == HAVEL_OK, "loadstring string.upper");
  if (rc == HAVEL_OK) {
    int top = havel_gettop(H);
    const char *s = havel_tostring(H, top - 1);
    check(s != nullptr && std::strcmp(s, "HELLO") == 0, "string.upper result");
  }

  // Error surface: a syntax error must surface as HAVEL_ERR, not a crash.
  rc = havel_loadstring(H, "this is not (valid havel {{{", "embed_bad");
  check(rc == HAVEL_ERR, "syntax error surfaces as HAVEL_ERR");

  havel_close(H);
  check(failures == 0, "no failures");
  return failures == 0 ? 0 : 1;
}
