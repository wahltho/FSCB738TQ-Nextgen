# AGENTS.md

This file is the quick-start context for coding agents working in this repo.
For full details, see:
- Documentation/Project_Log.md
- Documentation/FSC_User_Guide.md
- Documentation/FSC.md
- Documentation/FSC_Aircraft_Profile_Schema.md
- Documentation/FSCBuild.md
- README.md

## Project context
- Goal: standalone FSC B737 Throttle Quadrant plugin for X-Plane, focused on Zibo B737.
- Keep FSC logic in sync with the CPFlight plugin via shared code.
- This repo is a submodule of the CPFlight repo at external/FSCB738TQ-Nextgen.
- After changes here, update the CPFlight submodule pointer.

## Plugin identity
- Plugin name: FSCB738TQ-Nextgen
- Signature: com.fscb738tq.nextgen
- Command prefix: FSCB738TQ
- Prefs file: FSCB738TQ-Nextgen.prf
- Log file: fscb738tq_nextgen.log

## Build and deploy
- Build outputs: mac.xpl (macOS), lin.xpl (Linux), win.xpl (Windows).
- macOS universal binary is recommended.
- Build guide: Documentation/FSCBuild.md
- Deploy folder: deploy/FSCB738TQ-Nextgen

## Key paths (working machine)
- X-Plane root: /Volumes/X-Plane 12
- Plugin path: /Volumes/X-Plane 12/Resources/plugins/FSCB738TQ-Nextgen
- Preferences: /Volumes/X-Plane 12/Output/preferences/FSCB738TQ-Nextgen.prf
- Log file: /Volumes/X-Plane 12/Resources/plugins/FSCB738TQ-Nextgen/log/fscb738tq_nextgen.log

## Preferences and setup
- If the prefs file is missing, the plugin creates it with defaults and keeps fsc.enabled=0.
- Serial settings are installation-level (prefs), not profile-specific.
- Calibration values are written to prefs with a backup file (.bak).

## Profiles and schema
- Profiles live in <X-Plane>/Resources/plugins/FSCB738TQ-Nextgen/profiles/.
- Zibo profile is shipped in deploy/FSCB738TQ-Nextgen/profiles/zibo_b738.json.
- Matching is by exact tailnum (case-sensitive); no default profile.
- If no profile matches, FSC outputs stay disabled.
- Profiles are validated at startup; duplicates or errors disable FSC and are logged.

## FSC behavior highlights
- Calibration covers throttles, reversers, speedbrake, flaps detents (SEMIPRO).
- Throttle smoothing, deadband, and sync band prevent noise and asymmetry.
- Speedbrake logic matches legacy behavior (laminar/B738/flt_ctrls/speedbrake_lever).
- Auto-resync triggers after aircraft load (profile sync settings), including detent axis resync.

## Commands
- FSCB738TQ/fsc_calib_start
- FSCB738TQ/fsc_calib_next
- FSCB738TQ/fsc_calib_cancel
- FSCB738TQ/reload_prefs

## Open items / next steps
- Add more aircraft profiles beyond Zibo (future schema extensions if needed).
- Verify behavior on additional FSC hardware variants if available.
- Keep FSC and CPFlight in sync after any feature change.

## Notes for handoff
- This repo is FSC-only; avoid CPFlight references in public docs.
- If you update this repo, update the CPFlight submodule pointer afterwards.
