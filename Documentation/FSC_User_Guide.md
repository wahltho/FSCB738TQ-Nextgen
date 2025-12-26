# FSCB738TQ-Nextgen User Guide (EN)

Standalone X-Plane plugin for the FSC B737 throttle quadrant. Plugin version: 1.1.
This guide applies to the FSCB738TQ-Nextgen plugin only.

## 1) Overview
- Supported hardware types: FSC B737 Throttle Quadrant **SEMIPRO**, **PRO**, **MOTORIZED**.
- Target aircraft: Zibo B737 (uses Zibo-specific datarefs/commands).
- Plugin name shown in X-Plane: **FSCB738TQ-Nextgen**.
- Plugin signature (for X-Organizer/diagnostics): `com.fscb738tq.nextgen`.
- Command prefix (for bindings): `FSCB738TQ`.

## 2) Installation
1. Copy the plugin folder into `<X-Plane>/Resources/plugins/`:
   - From this repo: `deploy/FSCB738TQ-Nextgen` → `<X-Plane>/Resources/plugins/FSCB738TQ-Nextgen`
2. Inside the plugin folder, the `64/` directory contains platform binaries:
   - `mac.xpl` (macOS), `lin.xpl` (Linux), `win.xpl` (Windows). X-Plane loads the correct one automatically.
3. Start X-Plane and check **Plugins → FSCB738TQ-Nextgen**.

## 3) File locations
- Plugin folder: `<X-Plane>/Resources/plugins/FSCB738TQ-Nextgen/`
- Preferences file: `<X-Plane>/Output/preferences/FSCB738TQ-Nextgen.prf`
- Log file: `<X-Plane>/Resources/plugins/FSCB738TQ-Nextgen/log/fscb738tq_nextgen.log`
- Example prefs: `Documentation/FSCB738TQ-Nextgen.prf.example`

The log directory is created automatically if it does not exist.

## 4) Preferences (FSCB738TQ-Nextgen.prf)
The prefs file is plain text. Lines starting with `#` are comments. Unknown keys are ignored.
Edit the file with a text editor, then run `FSCB738TQ/reload_prefs` or use **Save & Apply** in the setup window.
If the prefs file is missing, the plugin creates it on startup with default values and keeps `fsc.enabled=0` for safety.

### 4.1 Minimal required settings
```ini
fsc.enabled=1
fsc.type=SEMIPRO        # SEMIPRO | PRO | MOTORIZED
fsc.port=COM3           # Windows: COM3, COM4, ...
                         # macOS: /dev/tty.usbserial-xxxx
                         # Linux: /dev/ttyUSB0 or /dev/ttyACM0
fsc.baud=19200
fsc.data_bits=8
fsc.parity=none         # none | even | odd
fsc.stop_bits=1
fsc.dtr=1
fsc.rts=1
fsc.xonxoff=0
```

### 4.2 Behavior and safety flags
- `fsc.fuel_lever_inverted=0|1`
  - Use `1` if the fuel cutoff levers behave inverted.
- `fsc.speed_brake_reversed=0|1`
  - Set automatically by calibration if the speedbrake direction is reversed.
- `fsc.throttle_smooth_ms` (default: 60)
  - Throttle-only smoothing (EMA). Use `0` to disable.
- `fsc.throttle_deadband` (default: 1)
  - Raw-count deadband for throttle noise. Use `0` to disable.
- `fsc.throttle_sync_band` (default: 0.015)
  - If L/R throttle difference is below this normalized band (0..1), both are averaged to prevent false asymmetry.
- `fsc.debug=0|1`
  - Enables extended logging and raw serial capture (see Logging section).

### 4.3 Calibration values
These are written by the calibration wizard and should normally not be edited by hand.

Speedbrake:
- `fsc.calib.spoilers_down`
- `fsc.calib.spoilers_armed`
- `fsc.calib.spoilers_min` (just above armed, start of travel)
- `fsc.calib.spoilers_detent` (flight detent)
- `fsc.calib.spoilers_up`

Throttles:
- `fsc.calib.throttle1_min`
- `fsc.calib.throttle1_full`
- `fsc.calib.throttle2_min`
- `fsc.calib.throttle2_full`

Reversers:
- `fsc.calib.reverser1_min`
- `fsc.calib.reverser1_max`
- `fsc.calib.reverser2_min`
- `fsc.calib.reverser2_max`

Flaps detents (SEMIPRO only):
- `fsc.calib.flaps_00`, `fsc.calib.flaps_01`, `fsc.calib.flaps_02`, `fsc.calib.flaps_05`,
  `fsc.calib.flaps_10`, `fsc.calib.flaps_15`, `fsc.calib.flaps_25`, `fsc.calib.flaps_30`, `fsc.calib.flaps_40`

### 4.4 Motorized tuning (advanced, MOTORIZED only)
Use these only if you need to fine-tune motor behavior. Incorrect values can cause rough motion.
- `fsc.motor.spoilers_down`, `fsc.motor.spoilers_up`
- `fsc.motor.throttle1_min`, `fsc.motor.throttle1_max`
- `fsc.motor.throttle2_min`, `fsc.motor.throttle2_max`
- `fsc.motor.trim_arrow_02`, `fsc.motor.trim_arrow_17`
- `fsc.motor.trim_wheel_02`, `fsc.motor.trim_wheel_17`
- `fsc.motor.throttle_update_rate_sec`

### 4.5 Logging
- `log.enabled=1|0` (defaults to 1)
- `log.file=fscb738tq_nextgen.log`

### 4.6 Aircraft profiles (schema and usage)
Profile files (JSON) define aircraft-specific mappings (datarefs/commands) for the FSC throttle quadrant.
The plugin loads profiles at startup and whenever you reload prefs. Profile loading and selection are
logged; set `fsc.debug=1` for detailed mapping logs.

**Location**
- `<X-Plane>/Resources/plugins/FSCB738TQ-Nextgen/profiles/`
- Example in this repo: `deploy/FSCB738TQ-Nextgen/profiles/zibo_b738.json`

**Selection (current behavior)**
- Each profile contains `aircraft_match.tailnums`, an exact, case-sensitive list.
- The active aircraft tail number comes from `sim/aircraft/view/acf_tailnum`.
- A profile is applied only when the tail number matches exactly.
- If there is no match, FSC outputs stay disabled.
- There is no manual override key; matching is tailnum-only.
- Unknown keys or validation errors disable FSC outputs until fixed.
- Example: Zibo often uses `ZB738` as the tail number. Always verify the exact tailnum in your sim.

**How to verify the active profile**
- Open the log file and look for a line that confirms the selected profile ID and tailnum.
- If no profile is selected, the log will state that no tailnum match was found.

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

**Example (minimal)**
```json
{
  "profile_id": "zibo_b738",
  "name": "Zibo 737-800X",
  "version": 1,
  "aircraft_match": { "tailnums": ["ZB738"] },
  "axes": {
    "throttle1": {
      "invert": false,
      "source_ref_min": "fsc.calib.throttle1_min",
      "source_ref_max": "fsc.calib.throttle1_full",
      "target_range": [0.0, 1.0],
      "targets": [
        { "type": "dataref", "path": "laminar/B738/axis/throttle1" }
      ]
    }
  }
}
```

## 5) Setup window (in X-Plane)
Open: **Plugins → FSCB738TQ-Nextgen → Open Window**

The setup window allows editing the most important FSC settings without leaving the sim.
- **Fields**: FSC enabled, type, port, baud, data bits, stop bits, parity, DTR/RTS/XONXOFF, fuel inversion, speedbrake reversed, debug.
- **Note**: throttle smoothing/sync settings are prefs-only (not in the UI).
- **Save & Apply**: writes the prefs file, creates a `.bak` backup, then reloads settings and reconnects.
- **Reload prefs**: re-reads the prefs file from disk and reconnects.
- **Calibration: Start / Next / Cancel**: runs the calibration wizard.
- **Status line**: shows the current calibration prompt or result.

## 6) Commands (bindable)
Use X-Plane command search or bind these to hardware:
- `FSCB738TQ/reload_prefs`
- `FSCB738TQ/fsc_calib_start`
- `FSCB738TQ/fsc_calib_next`
- `FSCB738TQ/fsc_calib_cancel`

## 7) Calibration workflow (detailed)
### 7.1 Before you start
- Load the Zibo 737 and wait until the cockpit is fully initialized.
- Ensure `fsc.enabled=1` and the correct `fsc.port` in the prefs.
- Run `FSCB738TQ/reload_prefs` (or **Save & Apply** in the window).

### 7.2 Step order (automatic)
The wizard always follows this sequence:
1. Speedbrake: **DOWN**
2. Speedbrake: **ARMED**
3. Speedbrake: **MIN** (just above ARMED, start of travel)
4. Speedbrake: **FLIGHT DETENT**
5. Speedbrake: **UP**
6. Throttle 1: **MIN/IDLE**
7. Throttle 1: **FULL**
8. Throttle 2: **MIN/IDLE**
9. Throttle 2: **FULL**
10. Reverser 1: **MIN/STOWED**
11. Reverser 1: **MAX/FULL REVERSE**
12. Reverser 2: **MIN/STOWED**
13. Reverser 2: **MAX/FULL REVERSE**
14. SEMIPRO only: Flaps detents **0, 1, 2, 5, 10, 15, 25, 30, 40**

### 7.3 How to capture values
- Move the requested control to the exact position.
- Hold steady for 1-2 seconds.
- Press **Next**.
- If the plugin reports "unstable" values, hold steady and press **Next** again.

### 7.4 After calibration
- The plugin writes all values into the prefs file and creates a `.bak` backup.
- `fsc.speed_brake_reversed` is set automatically if needed.
- Run **Reload prefs** (or `FSCB738TQ/reload_prefs`) to apply the new values immediately.
- Recalibrate after changing hardware type, swapping the quadrant, or if axis ranges drift.

## 8) What the plugin drives (Zibo)
The plugin writes Zibo throttle quadrant inputs directly into Zibo/XP datarefs/commands.
Key outputs include:
- Throttles: `laminar/B738/axis/throttle1`, `laminar/B738/axis/throttle2` and `sim/cockpit2/engine/actuators/throttle_ratio[0/1]`.
- Reversers: `laminar/B738/flt_ctrls/reverse_lever1`, `laminar/B738/flt_ctrls/reverse_lever2`.
- Speedbrake: `laminar/B738/flt_ctrls/speedbrake_lever` plus `sim/cockpit2/controls/speedbrake_ratio` and the discrete commands `sim/flight_controls/speed_brakes_up_one` / `sim/flight_controls/speed_brakes_down_one`.
- Flaps (SEMIPRO): detent commands to the Zibo flap commands.
- Latching switches and buttons: fuel levers, parking brake, TO/GA, autothrottle disengage, gear horn cutout.

## 9) Motorized type behavior
When `fsc.type=MOTORIZED`, the plugin can drive physical motors:
- Speedbrake motor auto-stow/auto-deploy based on aircraft state.
- Trim indicator and trim wheel assist.
- Motor outputs are suspended during calibration.
- Motor tuning values are raw device counts; adjust in small steps and test after each change.

## 10) Logging and debug
- Log file: `<X-Plane>/Resources/plugins/FSCB738TQ-Nextgen/log/fscb738tq_nextgen.log`
- Connection status and serial settings are logged on startup.
- With `fsc.debug=1`:
  - `FSC RAW: ...` captures 10 seconds of serial bytes after connect.
  - `FSC DBG: ...` prints mapped reverser values and calibration ranges.

## 11) Known behavior (Zibo)
- With engines off, Zibo clamps reverser levers to a small value. Full travel (0..1) is only available with engines running.
- Reverser smoothness depends on the raw FSC step resolution; coarse hardware steps produce stepped motion.

## 12) Troubleshooting
- **No movement**: check `fsc.enabled=1`, verify `fsc.port`, then run `FSCB738TQ/reload_prefs`.
- **Wrong fuel lever direction**: set `fsc.fuel_lever_inverted=1` and reload.
- **Reversers not reaching full**: ensure engines are running; check `FSC DBG` for mapped values near 1.0.
- **Speedbrake reversed**: run calibration again or set `fsc.speed_brake_reversed=1` manually.
- **Port busy**: close serial monitors or other tools; the port is exclusive while X-Plane is running.
- **No log file**: verify the plugin folder name and `log.enabled=1`.

## 13) Quick checklist for first-time setup
1. Install plugin to `<X-Plane>/Resources/plugins/FSCB738TQ-Nextgen/`.
2. Edit `<X-Plane>/Output/preferences/FSCB738TQ-Nextgen.prf` and set `fsc.port` and serial settings.
3. Start X-Plane, open **Plugins → FSCB738TQ-Nextgen → Open Window**.
4. Click **Reload prefs** and verify the port connects (check log).
5. Run calibration once.
6. Enjoy full hardware control of the Zibo throttle quadrant.
