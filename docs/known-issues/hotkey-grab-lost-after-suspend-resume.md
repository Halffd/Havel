# Hotkeys stop firing: a failed X11 key grab is silently swallowed and never retried

**Status:** root cause CONFIRMED from the user's live log; fix implemented
in the same change that adds `hotkey.list()[i].osGrabbed`.

## Symptom

The user reported: "after running long running scripts or servers with
run/runDetached, after 30~ minutes or more hotkeys randomly stop working."

## The user's environment (from their live log)

`/home/agent/.local/share/havel/logs/2026-11-13.log` (59,545 lines):

| observation | count | meaning |
|---|---|---|
| `IO::SetAnyKeyPressCallback: eventListener is null - callback will not be registered` | 5760 | the unified `EventListener` never started |
| `EventListener: using 'x11' input backend` -> `EventListener: Backend init failed` | many | the evdev path is unavailable, the X11 fallback is attempted |
| `X11 Error: BadAccess (attempt to access private resource denied) (code: 10, request: 2, minor: 0)` | 1 | havel's own `XSelectInput` on the root window was REFUSED by the X server |
| `[InputBridge] conditional 'F24' (id=3) initial SetHotkeyGrab = true` | many | every user hotkey is a CONDITIONAL hotkey registered through `InputBridge` |

`code: 10` is `BadAccess`; `request: 2` is the `ChangeWindowAttributes`
core request, which is exactly what `XSelectInput` issues. It appears
immediately after `EventListener: using 'x11' input backend`, i.e. it is
havel's own call.

Per the X11 protocol, `ChangeWindowAttributes` on the root window fails
with `BadAccess` when **another client holds an active keyboard or pointer
grab** - the request would be delivered to the grabbing client instead.

Consequences for the user's box:

- `globalEvdev = false` (`IO.cpp:609/673/676`), so `ParseHotkeyString`
  leaves `isEvdev = false` for every hotkey.
- `eventListener == nullptr`, so there is **no event delivery at all**.
- The ONLY way a hotkey can fire is the X11 key grab
  (`XGrabKey` on the root window) created by `IO::GrabHotkey`.

So a hotkey on this box is exactly one `XGrabKey` away from working, and
exactly one failed `XGrabKey` away from being dead.

## Root cause

### 1. `IO::GrabHotkey` discarded the grab result

`src/core/io/IO.cpp`, before the fix:

    bool regOk = ioBackend->RegisterHotkey(hotkey.key,
                        ioBackend->ToPlatformMask(hotkey.modifiers),
                        hotkey.type == HotkeyType::MouseButton);
    // ... regOk is never read ...
    hotkeys[hotkeyId].enabled = true;
    return true;                                     // reports success

`X11IOBackend::RegisterHotkey` returns false when any of its four
`XGrabKey` calls fails. `BadAccess` (code 10) is the common failure: some
other X client owns that key/modifier combination, or holds an active
keyboard grab. The binding was still marked enabled and the function still
returned true, so:

- `hotkey.list()` reported `grab == true`,
- the HotkeyModule context reported `grab == true`,
- but the X server held **no grab** for the key, so the keypress went to
  the other client and the hotkey never fired.

### 2. Nothing ever retried the failed grab

There was no retry path. `GrabHotkey` was called only from:

- `IO::Hotkey` (registration),
- `IO::EnableHotkey` / `IO::DisableHotkey` (explicit script calls),
- `IO::Suspend` / `IO::Resume` (the `suspend` hotkey feature),
- `HotkeyManager::resumeGrabs` (the REPL path only).

A conditional hotkey re-grabs only when its **condition changes**
(`SetHotkeyGrabById` -> `Havel.cpp:438`, `VM.cpp:2205`,
`ExecutionEngine.cpp:763`, `HavelEngine.hpp:598`). If the condition is
stable - which is the normal case for a hotkey bound to a mode - the
failed grab was never retried for the rest of the process lifetime.

That is the "30+ minutes then hotkeys randomly stop working" shape: the
grab fails once, when another client (one of the user's `run` /
`runDetached` children - a game, `xdotool`, a screencaster, an IME, a
terminal in raw mode) takes the keyboard, and never recovers.

### 3. The X11 error handler is a black hole

`src/core/io/X11IOBackend.cpp`:

    int X11IOBackend::XErrorHandler(Display *dpy, XErrorEvent *ee) {
        return 0;
    }

    bool X11IOBackend::Initialize() {
        XSetErrorHandler(XErrorHandler);   // replaces DisplayManager's handler
        ...
    }

`DisplayManager::Initialize()` installs `DisplayManager::X11ErrorHandler`,
which is the one that logs `X11 Error: BadAccess (code: 10, request: 2,
minor: 0)` (`DisplayManager.cpp:312`). `X11IOBackend::Initialize()` then
**replaces it** with the handler that discards every error.

Both backends share ONE X connection (`IOBackend::Create` passes
`DisplayManager::GetDisplay()` to `X11IOBackend`). So after
`ioBackend->Initialize()`, every X11 error on that connection is silently
dropped - including `BadAccess` from a failed `XGrabKey`. This is why the
user's log contains exactly one `BadAccess` (from `XSelectInput`, before
the backend was initialized) and **never** one for a hotkey grab.

### 4. Three different notions of "grab", and the real one was invisible

- `HotKey::grab` - the HotkeyManager/IO flag (what the script asked for).
- `HotkeyModule` context `grab` - the script-visible flag, set from the
  same condition result.
- the **OS grab** - the `XGrabKey` that actually exists on the X server.

The first two kept saying `true` while the third was gone. That is why
the failure was invisible from the script side, and why it took this long
to localise.

## Fix

1. **`IO::GrabHotkey` checks the grab result.** On failure it logs the
   hotkey alias, key and modifiers, removes the binding from the tracked
   OS-grab set, and returns false.

   The binding stays **ENABLED**. Enabled/grabbed is the *script's
   intent*; the window system's ability to honour it right now is a
   separate fact. Leaving the binding disabled would make it look
   intentional to the script and would make the retry loop skip it.

2. **A retry loop heals transient failures.** `IO::retryFailedGrabs()`,
   driven from `IO::PumpOnce()` every 5 seconds, re-attempts grabs that
   were requested but could not be established. `BadAccess` is transient:
   the other client eventually lets go, and the binding then works again
   without the user reloading the script.

   The selection rule (`IO::selectGrabsToRetry`, a pure function with a
   unit test) retries a binding only when it:

   - is in the *desired* set (the script asked for a grab - a `~`
     no-grab binding is never in this set),
   - does not currently hold an OS grab,
   - is enabled,
   - is not an evdev binding (evdev has no X11 grab),
   - has a valid keycode.

   The *desired* set is maintained separately from the *actual* set:
   `GrabHotkey` inserts into it, `UngrabHotkey` erases from it. That is
   what makes "the script wants a grab" survive a temporary failure while
   "the OS currently has a grab" reflects reality.

3. **`osGrabbed` makes the real state observable.** New:
   `IO::IsHotkeyGrabbed(id)` + `HotkeyManager::isOsGrabbed(alias)` + a new
   `osGrabbed` field on `hotkey.list()` entries report the REAL
   window-system grab. `grab` keeps its existing meaning; `osGrabbed` is
   the new, truthful one. The two are allowed to disagree - that
   disagreement IS the defect, made visible.

4. **`UngrabHotkey` / `UngrabAll` keep the tracked set in sync.**
   `UngrabAll()` issues `XUngrabKey(AnyKey, AnyModifier, root)`, which
   removes EVERY X11 grab on the server; the tracked set must follow or
   `osGrabbed` keeps reporting grabs that no longer exist. The *desired*
   set survives `UngrabAll()` on purpose: it is used to release grabs
   temporarily (the REPL suspends them so keystrokes reach the terminal),
   and the bindings still want to be grabbed afterwards.

5. **`X11IOBackend` no longer swallows X11 errors silently:** it logs the
   error code, the request and the minor code, so a `BadAccess` grab
   failure is visible in the log instead of vanishing.

6. **Unconditional script hotkeys now get an OS grab.**
   `InputBridge::handleHotkeyRegister` called
   `HotkeyManager::AddHotkey(key, cb)`, whose `int* outId` overload calls
   `io->AddHotkey(...)`. `IO::AddHotkey` only records the hotkey in the
   map; it never calls `GrabHotkey`. The grab happens in `IO::Hotkey` - a
   DIFFERENT entry point, reached only by `HostAPI::RegisterHotkey` and
   `HotkeyService::registerHotkey`.

   Consequence: a top-level `F24 => { ... }` hotkey was registered but
   held no X11 grab. On a box with a working evdev path that is invisible
   (the evdev EventListener delivers the key). On the user's box - no
   usable evdev, X11 only - such a hotkey could never fire.

   The fix calls `SetHotkeyGrabById(osId, true)` after registration, but
   **only when the registration asked for a grab**. A `~` prefix means
   "do NOT grab this key" (`ParsedHotkey::grab` defaults to true and is
   set to false by `~`), and the project's own scripts use it widely
   (`scripts/user/hotkeys0.3.hv`: `~^Down`, `~!Tab`, `~LButton`, `~w`).

   Two separate places had the `~` intent wrong:

   - `InputBridge::handleHotkeyRegister` grabbed unconditionally, so a
     `~` binding took the key away from every other application.
   - `HotkeyModule::createHotkeyContext`'s 8th parameter is `grab` (not
     `enabled` - the `enabled` parameter is filled in by the callee), and
     the call passed a hardcoded `true`. So the script-visible `grab`
     flag reported `true` for every registration, `~` or not.

   Both now use the registration's parsed intent, read once with
   `IO::ParseHotkeyString(hotkeyStr).grab`.

7. **`HotkeyManager::suspendGrabs` / `resumeGrabs` actually suspend.**
   `grabsSuspended_` was declared and read but **never assigned**, so it
   was always false. `SetHotkeyGrabById` therefore took the
   "apply the grab now" branch even while the grabs were suspended: a
   grab request arriving during a suspend immediately undid the suspend.
   `suspendGrabs` now sets `grabsSuspended_ = true` and `resumeGrabs`
   clears it.

   `resumeGrabs` then restores the wanted grabs. It reads `hk.grab`, the
   binding's intent, and that is already the post-suspend state:
   `SetHotkeyGrabById` always updates `hk.grab` / `hk.enabled`, even when
   it only records the request in `pendingGrabs_` instead of applying it
   immediately. So the intents that arrived during the suspend need no
   separate replay pass - `pendingGrabs_` is just cleared.

## Reproducers

`scripts/smoke/test_issue_hotkey_os_grab_state.hv`:
registers a plain hotkey, asserts that `hotkey.list()[i].osGrabbed`
exists, is a boolean, is consistent (false when no display is present -
not a stale true), and that the binding still fires and survives a
suspend/resume cycle.

`scripts/smoke/test_issue_hotkey_no_grab_prefix.hv`:
registers `~F23` and `F24`, asserts that the `~` binding reports
`grab == false` and the normal one `grab == true`, i.e. the no-grab
prefix is honoured.

`tests/io_hotkey_grab_retry_ut.cpp` (ctest `io_hotkey_grab_retry_test`):
7 unit tests over the pure retry-selection rule - a binding without an OS
grab is selected; one that already has a grab is not; a disabled binding
is not; an evdev binding is not; a binding without a keycode is not; an
unknown id is not; and only the failed binding is selected among many.

All three use F23/F24 + `hotkey.trigger` - no OS input is injected, and
no hardware state is touched. Headless-safe.

## Related, still open

- `X11IOBackend::RegisterHotkey` calls `UnregisterHotkey(keycode, modifiers)`
  first. `UnregisterHotkey` ungrabs the base modifier set plus the
  `{0, LockMask, numlock, numlock|Lock}` variants, but
  `ToPlatformMask` may already have OR-ed `LockMask` / `numlockMask_` into
  `modifiers`, so the plain-modifier variant is not ungrabbed and a stale
  grab can survive. Not the cause of this report; not yet fixed.
- `X11IOBackend` is an OUTPUT-only backend: it grabs keys but has no
  `GetInputFds()` / `OnFdsReady()` and delivers no key events. Key EVENT
  delivery lives in `X11Adapter`, which only `EventListener` creates, and
  `EventListener` only starts when evdev devices are found. On a box with
  no usable `/dev/input/event*` devices (the user's box), the X11 path
  grabs keys that nothing ever reads - the `eventListener is null -
  callback will not be registered` warning, 5760 times in one day of logs.
  Separate defect; real, but not what made the grabs disappear.
