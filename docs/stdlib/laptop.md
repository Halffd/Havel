---
title: "Laptop Module"
description: "Laptop hardware: battery with wear and time remaining, AC adapter, lid, ACPI power profile, suspend/hibernate, charge limits, keyboard backlight."
---

# Laptop Module

```hv
use laptop
```

**Source**: `modules/app/laptop.hv` (Havel, reading `/sys/class/power_supply`,
`/proc/acpi/button/lid`, `/sys/firmware/acpi/platform_profile`,
`/sys/power/state`, `/sys/class/leds`)

Everything reads Linux sysfs and procfs. **Absent hardware degrades to
`null`**, never throws, so one code path serves a laptop and a desktop: on a
workstation `laptop.battery()` is `null` and `laptop.summary()` still
returns a complete object with `status: "None"`.

The actions (`setProfile`, `suspend`, `setChargeLimit`, ...) **report**
instead of raising, because a sysfs write that was refused needs a reason
the user can act on, not a stack trace.

---

## Units

The values follow the kernel's `power_supply` ABI:

| Suffix | Unit |
|--------|------|
| `_now` on `voltage_` | microvolts (µV) |
| `_now` on `current_` | microamps (µA) |
| `_now` on `power_` | microwatts (µW) |
| `charge_*` | microamp-hours (µAh) |
| `energy_*` | watt-hours in µWh |

Both spellings are handled because vendors pick one or the other —
`charge_now`/`energy_now`, `charge_full`/`energy_full`. The returned
`powerWatts` and `timeRemainingMinutes` are already converted.

---

## Battery

```hv
b = laptop.battery()
print("${b.percent}% (${b.status}), ${b.timeRemainingMinutes} min left")
print("${b.powerWatts} W, wear ${b.wearPercent}%")
```

| Field | Type | Description |
|-------|------|-------------|
| `name` | `str` | sysfs device name (`BAT1`, `CMB0`, ...). |
| `path` | `str` | Its sysfs directory. |
| `percent` | `int` | Reported charge, 0-100. |
| `status` | `str` | `"Charging"`, `"Discharging"`, `"Full"`, `"Unknown"`. |
| `charging` `discharging` `full` | `bool` | Derived from `status`. |
| `level` | `str` | `capacity_level`: `"Normal"`, `"Low"`, `"Critical"`. |
| `now` | `int` | Charge in µAh or µWh. |
| `fullCapacity` | `int` | Charge at full, today. |
| `designCapacity` | `int` | Charge at full, by design. |
| `wearPercent` | `float` | `100 * (1 - full / design)`. |
| `cycleCount` | `int` | Charge cycles. |
| `voltage` | `int` | µV. |
| `current` | `int` | µA, magnitude only. |
| `powerWatts` | `float` | Derived: `power_now`, else `current x voltage`. |
| `timeRemainingMinutes` | `int` | Until empty (discharging) or full (charging). |
| `technology` | `str` | e.g. `"Li-ion"`. |
| `model` `manufacturer` `serial` | `str` | Battery identity. |
| `present` | `bool` | Is the battery physically present. |

`laptop.batteries()` returns every battery. `null` on a desktop.

**Time remaining is 0 when there is nothing to wait for** — a full battery,
or a device reporting no charge.

---

## AC adapter

```hv
if laptop.acOnline() {
  print("on mains")
}
a = laptop.ac()          // {name, path, online, type}
```

`acOnline()` is `false` when unplugged **and** when there is no mains
device at all, which is the conservative answer for "can I burn battery".

---

## Lid

```hv
if laptop.lidOpen() { /* resume */ }
l = laptop.lid()         // {name, path, state, open}
```

`state` is `"open"` or `"closed"`; `lidOpen()` is `false` for a closed lid
and for a machine with no lid switch, so "the lid is open" is only ever
true on positive evidence.

---

## Power profile

```hv
if laptop.profile() != null {
  print(laptop.profile(), laptop.profiles())
  laptop.setProfile("power-saver")
}
```

`profile()` reads `/sys/firmware/acpi/platform_profile` and returns `null`
on firmware that has none. `profiles()` lists the advertised choices.

`setProfile(name)` validates the name against the choices **before**
writing, so a typo reports `not one of balanced, power-saver` instead of a
refused write:

```hv
{ok: false, profile: "power-saverr",
 error: "not one of low-power, balanced, performance"}
```

---

## Suspend / hibernate

```hv
result = laptop.suspend()      // sleep to RAM
result = laptop.hibernate()    // sleep to disk
```

Writes `mem` / `disk` to `/sys/power/state` when it is writable (needs
root), otherwise falls back to `systemctl suspend` / `systemctl hibernate`,
which goes through logind and normally works unprivileged.

A write that succeeded but let the machine come straight back gives
`{ok: false, method: "suspend (sysfs)", error: "resumed - mem did not stick"}`.

**These really sleep the machine.** No test calls them.

---

## Charge limits

Some vendors cap charging to extend battery life. The sysfs paths differ
per vendor (ThinkPad, ASUS, ideapad), so they are globbed rather than
hard-coded.

```hv
l = laptop.chargeLimit()   // {start, end, startPath, endPath} or null
laptop.setChargeLimit(60)  // stop at 60%
```

`setChargeLimit` writes whichever threshold the vendor exposes — the start
threshold on devices that pair it with end=100, or the end threshold on
ASUS-style devices — and clamps to 0..100.

---

## Conservation mode

ASUS/Lenovo "stop at 60%" switch:

```hv
laptop.conservationMode()          // bool, or null when unsupported
laptop.setConservation(true)
```

---

## Keyboard backlight

```hv
k = laptop.keyboardBacklight()      // {device, current, max} or null
laptop.setKeyboardBacklight(2)      // clamped to max
```

---

## summary()

One call, one object, for status bars and low-battery logic. Absent
hardware is a `null` value in a present field — never a missing key:

```hv
s = laptop.summary()
// {battery, status, charging, acOnline, powerWatts,
//  timeRemainingMinutes, wearPercent, cycleCount, profile, lidOpen,
//  chargeLimit, conservationMode, keyboardBacklight,
//  keyboardBacklightMax}
print("${s.battery}% ${s.status} ${s.timeRemainingMinutes}min ac=${s.acOnline}")
```

---

## Notes

- Tests live in `scripts/tests/test_laptop_module.hv` and run explicitly
  through `hvtest`; they read hardware only and never suspend.
- For usage **percentages over time** (graphs, trend detection), pair this
  with the [Monitor Module](/stdlib/monitor)'s `watch`/`history` helpers.

---

**Previous:** [Monitor Module](/stdlib/monitor)
**Next:** [Hotkey Module →](/stdlib/hotkey)