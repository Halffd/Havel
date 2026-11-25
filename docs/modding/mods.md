# Native mods

Havel can load and hot-reload native `.so` mods, and a mod can install inline
hooks into other code at runtime. Everything in this document is driven from
the `mod` module:

```hv
use "mod"
use "ffi"

info = mod.load("~/mods/my_mod.so")     // load (or reload after edits)
info = mod.reload("my_mod")             // hot reload, state preserved
lib = ffi.open(info.loadedPath)         // call into the loaded instance
```

## What a mod is

A mod is a shared library exporting the protocol in `include/havel/mod.h`:

```c
int  havel_mod_init(HavelModApi *api);       // required
void havel_mod_unload(void);                 // optional
size_t havel_mod_state_size(void);           // optional
void havel_mod_state_save(void *buf);        // optional
int  havel_mod_reload(const void *state, size_t size); // optional
```

Built like any `.so` against the SDK header — no Havel build system needed:

```bash
cc -shared -fPIC -I<havel>/include my_mod.c -o libmy_mod.so
```

The file name becomes the mod's name (`libmy_mod.so` -> `my_mod`; the `lib`
prefix and extension are stripped).

## The `mod` module

| Function | Effect |
|---|---|
| `mod.load(path, opts?)` | Load a mod. `opts` is an object; `{"global": false}` loads it `RTLD_LOCAL` instead of `RTLD_GLOBAL`. Returns `{name, path, loadedPath, generation}`. |
| `mod.reload(name)` | Hot reload a loaded mod. Returns the new generation's info. |
| `mod.unload(name)` | Unload a mod. Returns true. |
| `mod.list()` | Array of loaded mods (same shape as `load` returns). |
| `mod.sym(modName, symbol)` | Resolve a symbol (`""` for global scope + loaded mods). Returns a pointer or null. |
| `mod.hook(symbol, replacement)` | Install a detour. Returns the trampoline: a callable pointer to the ORIGINAL. |
| `mod.unhook(symbol)` | Remove a detour by symbol. Returns true. |
| `mod.hooks()` | Array of `{symbol, target, replacement, trampoline, mod, targetMod}`. |

The trampoline `mod.hook` returns is a C pointer, not a Havel value — call it
with `ffi` (`ffi.call(trampoline, "int32", ["int32"], 7)`) or pass it back to
another C function. It is how a hook reaches the original code.

## Hot reload protocol

`mod.reload(name)` does, in order:

1. `havel_mod_state_size()` on the current instance -> byte count
2. `havel_mod_state_save(buf)` on the current instance
3. remove hooks that point into this mod's text
4. `havel_mod_unload()` on the current instance
5. `dlclose` the current generation
6. copy the mod file to a fresh generation path and `dlopen` it
7. `havel_mod_init(api)` on the new instance
8. `havel_mod_reload(buf, size)` on the new instance, when it exported
   `state_size` and the saved size is non-zero

Consequences worth knowing:

- **Static/global state does not survive** unless the mod round-trips it
  through `state_save`/`state_reload`. A counter that only lives in a static
  is 0 again after a reload.
- **Each generation is a private copy** of the `.so` under
  `$TMPDIR/havel_mods/<name>_g<N>.so`. That is what stops `dlopen` from
  returning a cached instance of stale text, and stops an in-place rewrite of
  the source file from corrupting the code that is mapped and running.
- The old generation's file is removed when it unloads; the newest few
  generations stay on disk while loaded.

## Hooking

`mod.hook(symbol, replacement)` patches the target function's prologue on
x86-64 Linux: five bytes are replaced by a `jmp` into a trampoline page
mmap'd within +-2GB of the target (the range `jmp rel32` reaches), the stolen
prologue bytes are relocated there followed by a jump back, and the gate at
the start of the page jumps to the replacement with a 12-byte
`mov rax, addr; jmp rax`. The target's bytes are written through
`/proc/self/mem` rather than by flipping page permissions:

- the trampoline needs no W^X window,
- `mprotect(PROT_WRITE)` on file-backed executable text is refused with an
  **uncatchable SIGSEGV** on some hardened hosts (observed on the development
  machine), which makes the mprotect dance a fallback rather than the primary
  path.

What a modder has to respect:

- **One detour per target.** Hooking a symbol that is already hooked is an
  error naming the current owner.
- **The replacement must match the target's calling convention.** The engine
  does not know the signature; it only moves the prologue.
- **Install and remove at init/unload time, not while other threads run
  inside the target.** The 5-byte patch is not written atomically with respect
  to a thread already executing at the target.
- **Remove hooks into a mod before it unloads.** The manager removes hooks it
  knows live in a mod's text (`targetMod` in `hook()`, visible from
  `mod.hooks()`) automatically, but a detour left installed into code that
  `dlclose` unmaps executes unmapped memory.
- **Do not hold other `dlopen` handles on the mod's file while reloading or
  unloading.** Any open handle is a reference: while one exists the
  generation stays mapped, and a later load at the same generation path can
  return the old instance — state appears to survive an unload. The test
  `scripts/tests/modding/test_mod_hot_reload.hv` closes its `ffi` handles at
  the right points for exactly this reason.

### Function entries that cannot be hooked

The prologue decoder relocates only position-independent instructions:
`push`/`pop`, register moves, `sub`/`add rsp, imm`, `lea` from `rsp`/`rbp`,
`xor`, `nop`s, `mov imm64`, `endbr64`. These are refused with an error
naming the offset and opcode:

- **RIP-relative or absolute-address instructions** — their displacement is
  only valid at the original address.
- **Control flow** (`call`, `jmp`, `ret`) — a relocated `call rel32` would
  call a different address.
- **A body shorter than the patch size** — a 4-byte `lea; ret` function has
  no prologue to steal. That refusal is correct, not a limitation to work
  around.

Anything else gets a precise error rather than a wrong hook.

## Resolution order

`mod.hook("demo_add", ...)` resolves the symbol in this order:

1. the mod whose `init` is running (a mod hooking its own export)
2. every loaded mod, most recently loaded generation first
3. the global scope (`RTLD_DEFAULT`), for host and system symbols

Step 3 is last deliberately: a `dlsym(RTLD_DEFAULT)` that lands in an
`RTLD_GLOBAL` library registers a scope reference glibc's `dlclose` can never
drop, so the library stays mapped and shadows later generations — a reload's
hook would patch text that no longer runs while its `resolve()` returns the
new address. `mod.sym(modName, symbol)` skips straight to that mod's own
handle.

## Limitations

- **x86-64 Linux for the engine.** On other platforms `mod.hook` reports
  "inline hooking requires x86-64 POSIX" and load/reload still work.
- **CET/IBT and shadow stacks are not handled.** A system that enforces
  indirect-branch tracking may reject the trampoline's jumps. This is the
  same limitation every user-space hooking library carries; there is no
  portable mitigation without compiler cooperation.
- **The `.dll` half of the name is the protocol, not the engine.** Windows
  loads and unloads mods through the same protocol; inline hooking is
  Linux/x86-64-only today.

## Testing

```bash
# Engine: decoder cases + a real install/call/unhook (host build, ~2s)
./scripts/ci/hook-engine-test.sh

# Lifecycle: load, detour, two hot reloads, state, unload, re-load
./scripts/ci/mod-lifecycle-test.sh

# End-to-end through the VM and the `mod` module
HAVEL_STDLIB=modules/std build-debug/havel run scripts/tests/modding/test_mod_hot_reload.hv

# RTLD_DEFAULT resolution from a library injected with LD_PRELOAD
HAVEL_BIN=/abs/path/to/havel HAVEL_STDLIB=/abs/path/to/modules/std ./scripts/ci/ld-preload-test.sh
```

`ld-preload-test.sh` preloads a probe `.so`, resolves its export with
`mod.sym("", ...)`, and calls it through `ffi`. It then reruns the same script
without `LD_PRELOAD` and requires the lookup to fail, so the test cannot pass
unless the preloaded object itself supplied the symbol.

`examples/mods/demo_mod.c` is the worked example: it hooks its own export at
init, round-trips state through the protocol, and unhooks on unload.