---
plan name: cpython-interop
plan description: Optional havel_python module linking libpython behind a bridge seam, keeping havel_core python-free
plan status: deferred
---

## Idea

Architecture decided. Do not build without a motivating script.

`havel_core` links no CPython. A separate optional `havel_python` module links
libpython only when built and loaded, exposing `py.call` / `py.import` as host
functions, mirroring the bridge seam the project already applied to every other
heavy dependency:

- `b4b0d8398` clipboard: move the qt clipboard into a backend factory; havel_core is qt-free
- `2faf1977e` move the qt ui backend out of havel_core
- `228b25660` guard: fail the build when Qt leaks back into the embeddable core
  (enforced as ctest `core-no-qt-guard`)

Linking libpython into the core would be the largest reversal of that pattern,
so option A (mandatory core dependency) is rejected. Option C (subprocess plus
IPC bridge) is rejected for in-process library calls: per-call serialization
plus process lifecycle, for no benefit over in-process.

## Blocked on

No motivating use case exists. Verified absence:

- `libpython|Py_Initialize|py.call|py.import|embed_python` over `src/` and
  `CMakeLists.txt`: zero hits
- no script, issue, or doc requests CPython interop. Python appears in `docs/`
  only as a syntax-style reference (Python-style modules, indentation, chained
  comparison caveats) and in the migration guide

Revisit only when a concrete Havel script needs a Python library. Until then
this is a written-down decision, not a backlog item.

## Prior art: IPC, not embedding

`havel-wm` is a git submodule in this tree and already speaks JSON IPC to Python
callers: `havel-wm/ipc_client.py`, `havel-wm/tests/runtime/ipc_probe.py`,
documented in `IPC_API.md` and `PROTOCOLS.md`.

Note the direction. Python calls *into* havel. Nothing yet needs havel to call
*out* to Python. The IPC precedent settles WM control; it does not settle
in-process library calls, which are a different use case.

## Risks to resolve before implementation

- **Core ABI blast radius.** `havel-wm` embeds `libhavel_core.a` and
  `libhavel_lang.a` (`src/host/module/BridgeSelection.hpp:3`), so a new module
  ABI touches the surface an external consumer depends on.
- **GIL policy** — undetermined whether Havel threads may call into CPython.
- **VM reentrancy** — whether `py.call` may re-enter the VM from inside a host
  function.
- **hvdb access is not free.** hvdb runs the real VM in-process, so bridge
  objects would be reachable, but inspecting a `PyObject*` needs
  `PyGILState_Ensure` while the VM holds its own state, and CPython 3.12+ exposes
  only a narrow debugger API.

## Implementation

Unwritten by design. A motivating script and a spec under `docs/specs/` come
first.