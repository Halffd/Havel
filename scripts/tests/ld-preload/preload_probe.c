/* Minimal LD_PRELOAD probe for scripts/ci/ld-preload-test.sh.
 *
 * The Havel side resolves this through mod.sym("", ...), which searches
 * RTLD_DEFAULT first. A preloaded shared object is in the global scope, so
 * the symbol must be visible there with no dlopen() and no mod load. The
 * value is deliberately distinctive so an accidental dlsym of another
 * library cannot satisfy the test.
 */
#define HAVEL_LD_PRELOAD_PROBE_VALUE 4242

__attribute__((visibility("default")))
int havel_ld_preload_probe(void) {
    return HAVEL_LD_PRELOAD_PROBE_VALUE;
}
