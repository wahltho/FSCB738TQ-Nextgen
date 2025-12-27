# FSC Aircraft Profile Schema (Draft)

This document defines a strict profile format that makes the FSC plugin usable
with multiple aircraft by moving all aircraft-specific datarefs/commands and
behavior into a profile JSON. Unknown keys are treated as errors.

## Goals
- Keep the FSC plugin generic and aircraft-agnostic.
- Allow per-aircraft mapping of axes, switches, indicators, and behaviors.
- Support multiple dataref targets per control (Zibo needs custom + standard).
- Keep calibration and installation-specific prefs separate from profiles.

## Loading Strategy
- Profiles live in a `profiles/` directory (JSON files).
- Auto-selection uses `aircraft_match.tailnums` only.
  There is no manual override key; matching is tailnum-only.
  Tailnums are exact, case-sensitive matches (no regex).
  If there is no match, FSC outputs stay disabled.

## Supported Input IDs
These IDs are the only valid keys under `axes` and `switches`.

### Axes (analog)
- `throttle1`
- `throttle2`
- `reverser1`
- `reverser2`
- `speedbrake` (used by `behaviors.speedbrake`)
- `flaps` (used by `behaviors.flaps`)

### Switches (digital/encoder)
- `fuel_cutoff_1`
- `fuel_cutoff_2`
- `parking_brake`
- `toga_left`
- `autothrottle_disengage`
- `gear_horn_cutout`
- `pitch_trim_wheel` (encoder)
- `el_trim_guard` (motorized only)
- `ap_trim_guard` (motorized only)

## Supported Indicator IDs
These IDs are the only valid keys under `indicators`.
- `parking_brake_light`
- `backlight`

## Top-Level Structure
```json
{
  "profile_id": "zibo_b738",
  "name": "Zibo 737-800X",
  "version": 1,
  "aircraft_match": {
    "tailnums": ["ZB738", "B738"]
  },
  "axes": { },
  "switches": { },
  "indicators": { },
  "behaviors": { },
  "sync": { },
  "notes": "Free text"
}
```

## Action Objects
Actions are used by switches and behavior blocks.

Command action:
```json
{ "type": "command", "path": "sim/flight_controls/speed_brakes_up_one", "phase": "once" }
```
- `phase`: `once` (default), `begin`, or `end`.

Dataref action:
```json
{ "type": "dataref", "path": "laminar/B738/flt_ctrls/speedbrake_lever", "value": 0.1 }
```
- For array datarefs, add `index` (integer) instead of using bracket syntax.

## Axes Mapping
Axes are for simple linear mappings (throttles and reversers).
Speedbrake and flaps are handled by behavior blocks.

Axis fields:
- `source_ref_min` (string, required): prefs key for minimum raw value.
- `source_ref_max` (string, required): prefs key for maximum raw value.
- `invert` (bool, optional, default false).
- `invert_ref` (string, optional): prefs key that supplies a boolean invert.
- `target_range` ([float,float], required): output range.
- `targets` (array, required): list of dataref targets.

Target fields:
- `type`: must be `dataref`.
- `path`: dataref path.
- `index` (optional): array index.

Constraints:
- Use either `invert` or `invert_ref`, not both.
- Unknown fields are invalid.

Example:
```json
"axes": {
  "throttle1": {
    "source_ref_min": "fsc.calib.throttle1_min",
    "source_ref_max": "fsc.calib.throttle1_full",
    "invert": false,
    "target_range": [0.0, 1.0],
    "targets": [
      { "type": "dataref", "path": "laminar/B738/axis/throttle1" },
      { "type": "dataref", "path": "sim/cockpit2/engine/actuators/throttle_ratio", "index": 0 }
    ]
  }
}
```

## Switches and Buttons
Switches map discrete inputs to actions.

### Latching
```json
"fuel_cutoff_1": {
  "type": "latching",
  "positions": {
    "on":  { "actions": [{ "type": "command", "path": "laminar/B738/engine/mixture1_idle" }] },
    "off": { "actions": [{ "type": "command", "path": "laminar/B738/engine/mixture1_cutoff" }] }
  }
}
```

Fields (latching):
- `positions.on.actions`
- `positions.off.actions`

### Latching Toggle (state-checked)
```json
"parking_brake": {
  "type": "latching_toggle",
  "state_dataref": "laminar/B738/parking_brake_pos",
  "state_on_min": 0.5,
  "positions": {
    "on":  { "actions": [{ "type": "command", "path": "laminar/B738/push_button/park_brake_on_off" }] },
    "off": { "actions": [{ "type": "command", "path": "laminar/B738/push_button/park_brake_on_off" }] }
  }
}
```

Fields (latching_toggle):
- `positions.on.actions`
- `positions.off.actions`
- `state_dataref` (optional but recommended): dataref to check current state.
- `state_on_min` (optional): threshold that means ON.

### Momentary
```json
"toga_left": {
  "type": "momentary",
  "on_press":   { "actions": [{ "type": "command", "path": "laminar/B738/autopilot/left_toga_press", "phase": "begin" }] },
  "on_release": { "actions": [{ "type": "command", "path": "laminar/B738/autopilot/left_toga_press", "phase": "end" }] }
}
```

Fields (momentary):
- `on_press.actions`
- `on_release.actions`

### Encoder
```json
"pitch_trim_wheel": {
  "type": "encoder",
  "on_cw":  { "actions": [{ "type": "command", "path": "laminar/B738/flight_controls/pitch_trim_up" }] },
  "on_ccw": { "actions": [{ "type": "command", "path": "laminar/B738/flight_controls/pitch_trim_down" }] }
}
```

Fields (encoder):
- `on_cw.actions`
- `on_ccw.actions`

## Indicators
Indicators drive known FSC outputs.

Fields:
- `type`: must be `led`.
- `source.dataref`: dataref to read.
- `on_when.min`: threshold to turn on.
- `invert`: flip on/off.

Example:
```json
"indicators": {
  "parking_brake_light": {
    "type": "led",
    "source": { "dataref": "laminar/B738/annunciator/parking_brake" },
    "on_when": { "min": 0.5 },
    "invert": false
  }
}
```

## Behavior Blocks (Advanced)
Behavior blocks enable full flexibility beyond simple axis mapping.
If a behavior is defined for an input, it overrides any axis mapping for that input.

### Speedbrake
Required fields:
- `source_axis`: must be `speedbrake`.
- `invert_ref` (optional): prefs key for inversion (use `fsc.speed_brake_reversed`).
- `ratio_dataref` (optional): dataref used for confirm/conditional logic.
- `detents`: must include `down`, `armed`, `up`.
- `analog`: maps the flight detent range to datarefs.

Detent fields:
- `source_ref` (prefs key).
- `tolerance` (raw counts).
- `actions` (array), or
  `actions_if_ratio_zero` + `actions_if_ratio_nonzero` (arrays, requires `ratio_dataref`).
- `confirm_value` (optional): compared against `ratio_dataref` when present.

Analog fields:
- `source_ref_min` / `source_ref_max` (prefs keys).
- `targets`: each target must have `path` and `target_range`.

Example:
```json
"behaviors": {
  "speedbrake": {
    "source_axis": "speedbrake",
    "invert_ref": "fsc.speed_brake_reversed",
    "ratio_dataref": "sim/cockpit2/controls/speedbrake_ratio",
    "detents": {
      "down": {
        "source_ref": "fsc.calib.spoilers_down",
        "tolerance": 2,
        "actions": [{ "type": "command", "path": "sim/flight_controls/speed_brakes_up_one" }],
        "confirm_value": 0.0
      },
      "armed": {
        "source_ref": "fsc.calib.spoilers_armed",
        "tolerance": 2,
        "actions_if_ratio_zero": [{ "type": "command", "path": "sim/flight_controls/speed_brakes_down_one" }],
        "actions_if_ratio_nonzero": [{ "type": "command", "path": "sim/flight_controls/speed_brakes_up_one" }],
        "confirm_value": -0.5
      },
      "up": {
        "source_ref": "fsc.calib.spoilers_up",
        "tolerance": 2,
        "actions": [{ "type": "command", "path": "sim/flight_controls/speed_brakes_down_one" }],
        "confirm_value": 1.0
      }
    },
    "analog": {
      "source_ref_min": "fsc.calib.spoilers_min",
      "source_ref_max": "fsc.calib.spoilers_detent",
      "targets": [
        {
          "type": "dataref",
          "path": "laminar/B738/flt_ctrls/speedbrake_lever",
          "target_range": [0.0889, 0.667]
        },
        {
          "type": "dataref",
          "path": "sim/cockpit2/controls/speedbrake_ratio",
          "target_range": [0.0, 0.99]
        }
      ]
    }
  }
}
```

### Flaps
Required fields:
- `source_axis`: must be `flaps`.
- `mode`: `nearest` or `exact`.
- `positions`: list of detents with actions.

Position fields:
- `name` (string).
- `source_ref` (prefs key).
- `tolerance` (raw counts).
- `actions` (array).

Example:
```json
"behaviors": {
  "flaps": {
    "source_axis": "flaps",
    "mode": "nearest",
    "positions": [
      {
        "name": "flaps_0",
        "source_ref": "fsc.calib.flaps_00",
        "tolerance": 2,
        "actions": [{ "type": "command", "path": "laminar/B738/push_button/flaps_0" }]
      }
    ]
  }
}
```

### Motorized
Motorized behavior is only active when `fsc.type=MOTORIZED` and `enabled=true`.
All motor calibration values are referenced from prefs.

```json
"behaviors": {
  "motorized": {
    "enabled": true,
    "throttle_follow": {
      "lock_dataref": "laminar/B738/autopilot/lock_throttle",
      "arm_dataref": "laminar/B738/autopilot/autothrottle_arm_pos",
      "lever_dataref": "laminar/B738/engine/thrust12_leveler",
      "motor_throttle1_min_ref": "fsc.motor.throttle1_min",
      "motor_throttle1_max_ref": "fsc.motor.throttle1_max",
      "motor_throttle2_min_ref": "fsc.motor.throttle2_min",
      "motor_throttle2_max_ref": "fsc.motor.throttle2_max",
      "update_rate_ref": "fsc.motor.throttle_update_rate_sec"
    },
    "speedbrake_motor": {
      "ratio_dataref": "sim/cockpit2/controls/speedbrake_ratio",
      "arm_ref": "fsc.calib.spoilers_armed",
      "up_ref": "fsc.calib.spoilers_up",
      "tolerance": 3,
      "ratio_up_min": 0.99,
      "ratio_down_max": 0.01,
      "motor_down_ref": "fsc.motor.spoilers_down",
      "motor_up_ref": "fsc.motor.spoilers_up",
      "hold_ms": 1500
    },
    "trim_indicator": {
      "wheel_dataref": "laminar/B738/flt_ctrls/trim_wheel",
      "wheel_min_ref": "fsc.motor.trim_wheel_02",
      "wheel_max_ref": "fsc.motor.trim_wheel_17",
      "arrow_min_ref": "fsc.motor.trim_arrow_02",
      "arrow_max_ref": "fsc.motor.trim_arrow_17",
      "hold_ms": 1500
    }
  }
}
```

## Sync and Initialization
```json
"sync": {
  "defer_until_datarefs": true,
  "startup_delay_sec": 10,
  "resync_on_aircraft_loaded": true,
  "resync_interval_sec": 1.0
}
```

## Serial Settings
Serial/COM parameters are not profile-specific. Keep them in the prefs file for
each installation.

## Calibration Separation
Keep calibration in the prefs file per user/hardware:
- `fsc.calib.*` values remain in the `.prf` file.
- Profiles reference them via `source_ref_*` keys.

## Pref Key References (Known)
Calibration keys:
- `fsc.calib.throttle1_min`, `fsc.calib.throttle1_full`
- `fsc.calib.throttle2_min`, `fsc.calib.throttle2_full`
- `fsc.calib.reverser1_min`, `fsc.calib.reverser1_max`
- `fsc.calib.reverser2_min`, `fsc.calib.reverser2_max`
- `fsc.calib.spoilers_down`, `fsc.calib.spoilers_armed`, `fsc.calib.spoilers_min`,
  `fsc.calib.spoilers_detent`, `fsc.calib.spoilers_up`
- `fsc.calib.flaps_00`, `fsc.calib.flaps_01`, `fsc.calib.flaps_02`, `fsc.calib.flaps_05`,
  `fsc.calib.flaps_10`, `fsc.calib.flaps_15`, `fsc.calib.flaps_25`, `fsc.calib.flaps_30`, `fsc.calib.flaps_40`

Motorized keys:
- `fsc.motor.spoilers_down`, `fsc.motor.spoilers_up`
- `fsc.motor.throttle1_min`, `fsc.motor.throttle1_max`
- `fsc.motor.throttle2_min`, `fsc.motor.throttle2_max`
- `fsc.motor.trim_arrow_02`, `fsc.motor.trim_arrow_17`
- `fsc.motor.trim_wheel_02`, `fsc.motor.trim_wheel_17`
- `fsc.motor.throttle_update_rate_sec`

Behavior flags:
- `fsc.speed_brake_reversed`

## Validation Rules (Planned)
- Required keys: `profile_id`, `name`, `version`, `aircraft_match.tailnums`.
- All profiles must have unique `profile_id`.
- `aircraft_match.tailnums` must not overlap across profiles.
- Unknown keys or invalid types reject the profile.
- Any validation error disables FSC outputs until fixed.
