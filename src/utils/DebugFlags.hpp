#pragma once

namespace havel::debugging {

// System-level debug flags (shared between havel_core and havel_lang)
inline bool debug_io = false;
inline bool debug_hotkeys = false;
inline bool debug_evdev = false;
inline bool debug_event_listener = false;

// Trace per-stage hotkey latency (kernel read -> match -> callback).
// Off by default: logging to stdout itself costs ms, so only enable while
// measuring, never during normal key spam.
inline bool trace_hotkey_latency = false;

// Trace VM::emitVariableChanged. Off by default: every array/object write
// fires this, and the SKIPPED (no handler) branch alone floods the log with
// hundreds of ms of print per scheduler tick when reactive hotkeys aren't
// registered. Enable only when debugging conditional-hotkey dependency
// tracking.
inline bool debug_var_changed = false;

}
