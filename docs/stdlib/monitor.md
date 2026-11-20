---
title: "Monitor Module"
description: "System resource monitoring: CPU, RAM, disk, network, processes, temperatures, battery and GPU — sampled from procfs and sysfs."
---

# Monitor Module

```hv
use monitor
```

**Source**: `modules/app/monitor.hv` (Havel, reading `/proc` and `/sys`)
with `libc` sysinfo/getloadavg resolved over FFI.

Everything here is a **snapshot read**. Absent hardware returns `null` (a
desktop has no battery; a VM may have no hwmon) rather than an empty object
or an error, so one code path serves a laptop and a workstation.

---

## CPU

| Function | Signature | Description |
|----------|-----------|-------------|
| `monitor.cpu()` | `() -> obj` | Aggregate CPU usage and per-jiffie totals. |
| `monitor.cpuPerCore()` | `() -> array` | Per-core totals, one object per core. |

`cpu()` computes usage **between calls** — it stores the previous snapshot
in a module-level `_prev_cpu`, so the first call after a load reports
`usage: 0.0` because there is nothing to diff against. Call it twice.

```hv
c1 = monitor.cpu()          // usage 0.0, no previous sample
sleep(1000)
c2 = monitor.cpu()          // now meaningful
print(c2.usage)             // 12.5  (%)
print(c2.cores)             // 16
print(c2.freq)              // 2400.0 (MHz)
```

Fields: `usage`, `user`, `nice`, `system`, `idle`, `iowait`, `irq`,
`softirq`, `steal`, `cores`, `freq`.

---

## Memory & disk

| Function | Signature | Description |
|----------|-----------|-------------|
| `monitor.ram()` | `() -> obj` | `{total, used, free, available, shared, buffcache, percent}` in bytes. |
| `monitor.swap()` | `() -> obj` | `{total, used, free, percent}` in bytes. |
| `monitor.load()` | `() -> obj` | Load average. Keys are `1`, `5`, `15` — **integers, not strings**. |
| `monitor.disk(path)` | `(str) -> obj` | One mount point. Defaults to `"/"`. |
| `monitor.disks()` | `() -> array` | Every mount from `df`. |

```hv
r = monitor.ram()
print(r.used / 1024 / 1024 / 1024)     // GB used
print(r.available / 1024 / 1024 / 1024, "GB available")

d = monitor.disk("/")
print(d.used, d.total, d.percent, d.mount)   // percent is already a float

// load() has integer keys, so subscript rather than dot:
l = monitor.load()
print(l[1], l[5], l[15])
```

`disk()` and `disks()` return `{total, used, free, percent, mount}` —
`disks()` adds `device`. `percent` is a `float` already stripped of its
`%`, so `"72%"` comes back as `72.0`.

---

## Processes

| Function | Signature | Description |
|----------|-----------|-------------|
| `monitor.processCount()` | `() -> int` | Running processes, from `sysinfo`. |
| `monitor.processList()` | `() -> array` | **PIDs only** — an array of ints. |
| `monitor.processInfo(pid)` | `(int) -> obj?` | Detail on one, or `null`. |
| `monitor.topProcesses(sortBy, limit)` | `(str, int) -> array` | Sorted leaderboard. |

`topProcesses` sorts by `"rss"` (default), `"vmSize"`, `"cpu"` or
`"threads"`, and takes 10 by default.

```hv
count = monitor.processCount()
info = monitor.processInfo(1)          // null for a pid that vanished
print(info.name, info.rss / 1024, "KB", info.state)

for p in monitor.topProcesses("rss", 5) {
  print(p.name, p.rss / 1024 / 1024, "MB")
}
```

`processInfo` returns `{pid, name, state, ppid, threads, rss, vmSize,
vmRSS, utime, stime, cmdline}`.

---

## Network & temperatures

| Function | Signature | Description |
|----------|-----------|-------------|
| `monitor.network()` | `() -> array` | Per-interface counters. |
| `monitor.temperatures()` | `() -> array` | Every thermal zone and hwmon input. |

`network()` elements are `{name, rxBytes, rxPackets, rxErrors, rxDropped,
txBytes, txPackets, txErrors, txDropped}`.

`temperatures()` elements are `{zone, type, temp}` with `temp` in °C as a
`float`, gathered from `/sys/class/thermal/thermal_zone*` and
`/sys/class/hwmon/*/temp*_input`.

```hv
for t in monitor.temperatures() {
  print("${t.zone} (${t.type}): ${t.temp} °C")
}
// acpitz (acpitz): 44.0 °C
// hwmon0 (coretemp Package id 0): 52.0 °C
```

---

## Battery (basic)

```hv
b = monitor.battery()
print(b.percent, b.status, b.voltage)
```

Returns `{percent, status, powerRate, voltage}`, or `null` when there is no
battery. `powerRate` is 0 on hardware that does not expose `power_now` —
for time remaining, derived power, wear and cycles use the
[Laptop Module](/stdlib/laptop), which models the whole device.

---

## GPU

`monitor.gpu()` queries `nvidia-smi`; returns `null` when it is absent or
the output is short (i.e. on any non-NVIDIA machine).

---

## Caches and custom metrics

| Function | Signature | Description |
|----------|-----------|-------------|
| `monitor.refresh()` | `() -> nil` | Drop the window caches. |
| `monitor.metric(name, value)` | `(str, any) -> nil` | Record a custom metric. |
| `monitor.metricGet(name)` | `(str) -> any` | Read one back. |
| `monitor.metricDel(name)` | `(str) -> bool` | Delete one. |
| `monitor.metrics()` | `() -> obj` | All metrics. |
| `monitor.watch(name, cb, intervalMs)` | `(str, fn, int) -> nil` | Poll a callback. |
| `monitor.unwatch(name)` | `(str) -> bool` | Stop watching. |
| `monitor.watchTick(name)` | `(str) -> any` | Run one tick. |
| `monitor.watchResult(name)` | `(str) -> any` | Last tick's result. |
| `monitor.watchList()` | `() -> array` | Active watches. |
| `monitor.snapshot()` | `() -> obj` | Everything in one object. |
| `monitor.history(name, value)` | `(str, any) -> nil` | Append a sample. |
| `monitor.historyGet(name)` | `(str) -> array` | A series. |
| `monitor.historyClear(name)` | `(str) -> nil` | Drop a series. |
| `monitor.historyNames()` | `(str) -> array` | Series names. |
| `monitor.uptime()` | `() -> num` | Uptime in seconds. |

---

**Previous:** [Brightness Module](/stdlib/brightness)
**Next:** [Laptop Module →](/stdlib/laptop)