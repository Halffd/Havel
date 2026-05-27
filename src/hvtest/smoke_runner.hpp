#pragma once

namespace hvtest {

int run_smoke_tests(int argc, char **argv);

#ifdef HAVEL_ENABLE_LLVM
int run_jit_smoke_tests(int argc, char **argv);
#endif

}
