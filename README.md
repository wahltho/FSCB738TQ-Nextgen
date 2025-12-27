# FSCB738TQ-Nextgen

Standalone X-Plane plugin for the FSC B737 Throttle Quadrant. Focused on the Zibo B737, but supports other aircraft via aircraft profiles.
No additional lua scripts required.

## Features
- Supports FSC B737 Throttle Quadrant: SEMIPRO, PRO, MOTORIZED.
- Serial/COM configuration via prefs and in-sim setup window.
- Calibration wizard for throttles, reversers, speedbrake, and flaps detents.
- Throttle smoothing, deadband, and sync band to reduce noise/asymmetry.
- Aircraft profile system (JSON) with exact tailnum matching.
- Zibo profile included in `FSCB738TQ-Nextgen/profiles/`.
- Logging to file for diagnostics.

## Requirements
- X-Plane 12.
- FSC B737 Throttle Quadrant connected via serial (FTDI).

## Support Status
- Supported platforms: macOS (universal), Windows x64, Linux x86_64.
- Tested so far: Windows + FSC B737 Throttle Quadrant SEMIPRO only.
- Other platform/hardware combinations are currently untested.

## Disclaimer
Provided "as is", without warranty of any kind. Use at your own risk; no liability for damages.

## Install
1. Copy the plugin folder into X-Plane:
   - From zip file: `FSCB738TQ-Nextgen` -> `<X-Plane>/Resources/plugins/FSCB738TQ-Nextgen`
2. The `64/` folder contains the platform binaries:
   - `mac.xpl`, `lin.xpl`, `win.xpl`
3. Start X-Plane and open **Plugins -> FSCB738TQ-Nextgen**.

## Quick Start
1. Open the setup window (Plugins -> FSCB738TQ-Nextgen).
2. Enable FSC and set the serial port/baud.
3. Run the calibration wizard.
4. Ensure your aircraft tail number matches a profile in `profiles/`.

## Preferences
- Prefs file: `<X-Plane>/Output/preferences/FSCB738TQ-Nextgen.prf`
- If the prefs file is missing, the plugin creates it with defaults and keeps `fsc.enabled=0` for safety.
- Reload prefs via command `FSCB738TQ/reload_prefs` or the setup window.

## Aircraft Profiles
- Profiles live in `<X-Plane>/Resources/plugins/FSCB738TQ-Nextgen/profiles/`.
- Matching is exact and case-sensitive via `aircraft_match.tailnums`.
- If no profile matches, FSC outputs stay disabled.
- Schema: `Documentation/FSC_Aircraft_Profile_Schema.md`.

## Logs
- Log file: `<X-Plane>/Resources/plugins/FSCB738TQ-Nextgen/log/fscb738tq_nextgen.log`

## Build
See `Documentation/FSCBuild.md` for macOS (universal), Linux, and Windows builds.

## Docs
- User guide: `Documentation/FSC_User_Guide.md`
- Technical notes: `Documentation/FSC.md`
- Profile schema: `Documentation/FSC_Aircraft_Profile_Schema.md`

## License
MIT License. See `LICENSE`.
