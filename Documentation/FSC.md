# FSCB738TQ-Nextgen – FSC Throttle Quadrant (EN)

This document covers FSCB738TQ-Nextgen setup, prefs, commands, calibration, logging, and profiles. Plugin version: 1.1.

## Quick start
- Copy `Documentation/FSCB738TQ-Nextgen.prf.example` to `<X-Plane>/Output/preferences/FSCB738TQ-Nextgen.prf`.
- If the prefs file is missing, the plugin auto-creates it on first run with defaults and keeps `fsc.enabled=0`.
- Key prefs:
  - `fsc.enabled=1`
  - `fsc.type=SEMIPRO|PRO|MOTORIZED`
  - `fsc.port=COM3` (or `/dev/tty.usbserial` on mac/Linux)
  - Serial defaults: `baud=19200`, `data_bits=8`, `parity=none`, `stop_bits=1`, `dtr=1`, `rts=1`, `xonxoff=0`
  - `fsc.fuel_lever_inverted=0|1` (invert the active-low fuel levers)
  - `fsc.debug=0|1` (enable extra logging + RAW capture on connect)
- After editing prefs in-flight: run command `FSCB738TQ/reload_prefs` to re-open the port and reload settings.

## Commands (X-Plane)
- `FSCB738TQ/reload_prefs` — reloads `FSCB738TQ-Nextgen.prf` and reconnects the device.
- `FSCB738TQ/fsc_calib_start`
- `FSCB738TQ/fsc_calib_next`
- `FSCB738TQ/fsc_calib_cancel`

The wizard speaks a prompt for each step; captured values are printed to the log for verification.
Menus:
- **Plugins → FSCB738TQ-Nextgen**

## Calibration notes
- Supported for all FSC types. Steps include speedbrake, both throttles, both reversers, and (for SemiPro) flap detents.
- During calibration no axis outputs are sent to Zibo.
- After finishing, the plugin writes the values into the active prefs file and creates a `.bak` backup.
- The log still prints lines like `fsc.calib.reverser1_min=...` for verification.

## Logging
- Prefs: `log.enabled=1`, `log.file=fscb738tq_nextgen.log`.
- With `fsc.debug=1`, the plugin:
  - Captures 10s of RAW serial bytes on connect (`FSC RAW: ...`).
  - Logs reverser mapping: `FSC DBG: rev1 raw=34 mapped=1.00 min=17 max=34`.
- Log location: `<X-Plane>/Resources/plugins/FSCB738TQ-Nextgen/log/fscb738tq_nextgen.log`.

## Datarefs / outputs (FSC)
- Throttles (all FSC types, when throttle motor is not active):  
  `laminar/B738/axis/throttle1`, `laminar/B738/axis/throttle2`, and `sim/cockpit2/engine/actuators/throttle_ratio` indices 0/1.
- Reversers: `laminar/B738/flt_ctrls/reverse_lever1` / `reverse_lever2` (0..1).
- Speedbrake: uses `sim/cockpit2/controls/speedbrake_ratio` plus `sim/flight_controls/speed_brakes_*_one` for DOWN/ARMED/UP, and writes `laminar/B738/flt_ctrls/speedbrake_lever` for travel (MIN..DETENT); auto-detects reversed direction during calibration.
- Latching switches (active low): fuel levers, parking brake, TO/GA, A/T disengage, gear horn cutout.
- Motorized (type=MOTORIZED): drives speedbrake motor, trim indicator, and handles stab-trim guard switches via Zibo commands.

## Pref keys (FSC excerpt)
- Enable/type/port: `fsc.enabled`, `fsc.type=SEMIPRO|PRO|MOTORIZED`, `fsc.port`.
- Serial: `fsc.baud`, `fsc.data_bits`, `fsc.parity`, `fsc.stop_bits`, `fsc.dtr`, `fsc.rts`, `fsc.xonxoff`.
- Behavior: `fsc.fuel_lever_inverted`, `fsc.speed_brake_reversed`, `fsc.debug`.
- Throttle stability: `fsc.throttle_smooth_ms`, `fsc.throttle_deadband`, `fsc.throttle_sync_band`.
- Calibration values: `fsc.calib.*` (spoilers, throttles, reversers, flaps for SemiPro).
- Motorized tuning (if applicable): `fsc.motor.*` (speedbrake positions, trim indicator, etc.).

## Aircraft profiles (schema and usage)
Profile files (JSON) define aircraft-specific mappings (datarefs/commands) for the FSC throttle quadrant.
The plugin loads profiles at startup and whenever you reload prefs. Profile loading and selection are
logged; set `fsc.debug=1` for detailed mapping logs.

**Location**
- `<X-Plane>/Resources/plugins/FSCB738TQ-Nextgen/profiles/`
- Example file in this repo: `deploy/FSCB738TQ-Nextgen/profiles/zibo_b738.json`

**Selection (current behavior)**
- Each profile contains `aircraft_match.tailnums`, an exact, case-sensitive list.
- The active aircraft tail number comes from `sim/aircraft/view/acf_tailnum`.
- A profile is applied only when the tail number matches exactly.
- If there is no match, FSC outputs stay disabled.
- There is no manual override key; matching is tailnum-only.
- Unknown keys or validation errors disable FSC outputs until fixed.

**Supported input IDs**
- Axes: `throttle1`, `throttle2`, `reverser1`, `reverser2`, `speedbrake`, `flaps`.
- Switches: `fuel_cutoff_1`, `fuel_cutoff_2`, `parking_brake`, `toga_left`,
  `autothrottle_disengage`, `gear_horn_cutout`, `pitch_trim_wheel`,
  `el_trim_guard`, `ap_trim_guard`.
- Indicators: `parking_brake_light`, `backlight`.

**Top-level keys**
- `profile_id` (string): unique identifier used in logs and diagnostics.
- `name` (string): human-friendly profile name.
- `version` (number): schema version for future migrations.
- `aircraft_match` (object): match rules; currently only `tailnums`.
- `axes` (object): analog inputs mapped to datarefs.
- `switches` (object): discrete inputs mapped to commands or datarefs.
- `indicators` (object): datarefs mapped to LEDs or lamps.
- `behaviors` (object): advanced behavior blocks (speedbrake, flaps, motorized).
- `sync` (object): initialization and resync behavior.
- `notes` (string): free text.

**aircraft_match**
- `tailnums`: array of exact strings, case-sensitive.
- Keep it minimal; do not use regex fields.

**Serial settings**
- Serial/COM parameters are not profile-specific.
- They stay in the prefs file and apply to the local installation only.

**axes**
- Each axis entry has:
  - `invert` (bool): invert raw input before mapping.
  - `invert_ref` (string, optional): prefs key that supplies a boolean invert
    (for example `fsc.speed_brake_reversed`).
  - `source_ref_min` / `source_ref_max`: reference calibration keys from prefs
    (for example `fsc.calib.throttle1_min` / `fsc.calib.throttle1_full`).
  - `target_range` [min,max]: normalized output range, typically 0.0..1.0.
  - `targets`: array of outputs; each output is `{ "type": "dataref", "path": "..." }`.
- The same axis can write multiple targets (Zibo needs both custom and standard datarefs).
- Calibration values in the prefs still define the real hardware min/max; profiles should not embed per-user calibration.
- Axes can be used for throttles, reversers, and optionally speedbrake or flaps.
- If a behavior block exists for an input, any axis mapping for that input is ignored.
- For array datarefs, add `index` to the target object.

**action objects**
- Actions are shared by `switches`, `detents`, and `behaviors`.
- Command action:
  - `{ "type": "command", "path": "sim/flight_controls/speed_brakes_up_one", "phase": "once" }`
  - `phase` can be `once`, `begin`, or `end` (defaults to `once`).
- Dataref action:
  - `{ "type": "dataref", "path": "laminar/B738/flt_ctrls/speedbrake_lever", "value": 0.1 }`
  - For array datarefs, use `index` instead of bracket syntax.

**switches**
- `type` controls how events are interpreted:
  - `latching`: two stable positions.
  - `latching_toggle`: one hardware state toggles the same command (used when the sim uses a toggle command).
  - `momentary`: press/release events.
  - `encoder`: two directions (cw/ccw) for trim wheels or knobs.
- `latching_toggle` can include `state_dataref` and `state_on_min` to avoid redundant toggles.
- Actions:
  - `command`: X-Plane command path.
  - `dataref` + `value`: set a dataref directly (float or int).

**indicators**
- LED outputs are driven from sim datarefs.
- Fields:
  - `source.dataref`: dataref to read.
  - `on_when.min`: minimum value to turn on (for example 0.5).
  - `invert`: flip on/off.

**sync**
- `defer_until_datarefs`: wait for aircraft datarefs before applying.
- `startup_delay_sec`: extra delay to allow aircraft init.
- `resync_on_aircraft_loaded`: re-apply profile on aircraft load.
- `resync_interval_sec`: periodic resync (seconds).

**behaviors (advanced)**
- Used when you need full flexibility beyond basic axis mapping.
- If a behavior exists for an input, any axis mapping for that input is ignored.
- Typical blocks:
  - `speedbrake`: detents with command actions + analog range mapping to datarefs.
    Supports `ratio_dataref` and conditional actions for the ARMED logic.
  - `flaps`: detent list with nearest/exact matching.
  - `motorized`: trim indicator, speedbrake motor, and throttle follow.
- Behavior blocks should reference prefs for per-installation motor calibration
  (for example `fsc.motor.*`) and not embed raw motor numbers.

**validation (current)**
- Unique `profile_id` across all profiles.
- No duplicate tailnums across profiles.
- Any validation error disables FSC outputs until fixed.

## Known Zibo behavior
- With engines off, Zibo may clamp reverser levers to a small value (~0.06). With engines running, full 0..1 travel is applied.
