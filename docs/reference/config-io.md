# Config keys: `[IO]` section

Read from `~/.config/havel/havel.cfg`. All keys have in-code defaults; the
section only needs to exist when you override a default.

| Key | Type | Default | Purpose |
|-----|------|---------|---------|
| `IO.Backend` | string | `evdev` | Input backend: `evdev`, `x11`, `wayland`, `windows`, `auto`. Same as the `--io` CLI flag. |
| `IO.Executor` | string | `Scheduler` | Input dispatch executor. |
| `IO.GrabDelay` | int (ms) | `0` | Settle delay applied after all input devices are grabbed, before event forwarding is considered warm. Raises it if a compositor/login tool re-grabs devices briefly during startup. |

## Grab behaviour (evdev)

Devices are grabbed with `EVIOCGRAB` only after the event loop is confirmed
draining, so grabbed events cannot black-hole. A failed grab is retried up to
3 times (150 ms apart) to ride out transient `EBUSY` from another process
holding the grab at startup. The result is logged at info level:

```
EventListener: Grabbed 2/2 input devices
```

An ungrabbed device leaks its events to the desktop (hotkeys on it stay
dead) - the summary says so explicitly when the count is short.

Debug the whole grab path (grab lifecycle, retries, evdev reads) with:

```bash
havel --debug-events   # or -dev
```
