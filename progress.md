# Progress

## Status
Review complete

## Tasks

### Motor Vibration Tool — Physics & Vibration Soundness Review

Reviewed files:
- `src/common/feature/motor_vibration/motor_vibration_config.hpp`
- `src/common/feature/motor_vibration/motor_vibration_wizard.cpp`
- `lib/Marlin/Marlin/src/gcode/calibrate/M958.hpp`
- `lib/Marlin/Marlin/src/gcode/calibrate/M958.cpp`
- `src/common/feature/manual_belt_tuning/manual_belt_tuning_wizard.cpp`
- `src/common/feature/manual_belt_tuning/manual_belt_tuning_config.hpp`

## Findings Summary

### BLOCKER: MicrostepRestorer constructed in wrong order (step_len 8x too large)

In `motor_vibration_wizard.cpp:59-63`, the wizard:
1. Sets `stepper_microsteps(X_AXIS, 128)` and `stepper_microsteps(Y_AXIS, 128)`
2. Then constructs `MicrostepRestorer microstep_restorer` (line 63)

`MicrostepRestorer` captures the **current** microstep resolution at construction time
(M958.cpp:1295-1298). Since it's constructed AFTER the 128 change, it saves 128 as the
"original" mres. `get_step_len()` then computes:

```
mm_per_step = planner.mm_per_step[axis] * orig_mres / current_mres
            = mm_per_step * 128 / 128
            = mm_per_step   (which is mm per FULL step at default 16 ustep)
```

This should be `mm_per_step * 16 / 128` (mm per microstep at 128 resolution).
The step_len is **8x too large** (for CoreOne with default mres=16).

Consequence: `amplitudeRoundToSteps(amplitude, step_len)` computes 8x fewer steps
than intended, producing vibration at ~8x lower physical amplitude than designed.

Compare: belt tuning wizard constructs `MicrostepRestorer` at top of `run_from_gantry()`
(line 42), BEFORE the 128 microstep change (line ~81). M958 also constructs it before
`setup_axis()`.

### NOTE: No homing before vibration

The motor vibration wizard does not home or move to a known position before vibrating.
The belt tuning wizard homes (G28) and moves to `calib_position_x/y` before vibrating.
M959 also homes unless the 'D' flag is passed.

Without homing, the toolhead position is unknown. The vibration amplitude is very small
(max ~0.3 mm at 20 Hz, ~0.03 mm at 80 Hz), so crash risk is negligible. However,
if the toolhead happens to be at a limit, vibration into the hard stop could cause
mechanical stress or stall. The belt tuning wizard's homing+centering eliminates this.

### NOTE: Strobe removal is clean

No references to `strobe` exist in the motor vibration feature directory. The belt
tuning wizard uses `buddy::xbuddy_extension().set_strobe()` for visual resonance
detection — the motor vibration tool intentionally omits this since it's for subjective
"feel/listen" resonance hunting, not visual stroboscopic matching. No orphaned strobe
references found.

## Files Changed
- `src/common/feature/motor_vibration/motor_vibration_config.hpp` (new)
- `src/common/feature/motor_vibration/motor_vibration_wizard.cpp` (new)
- `src/common/feature/motor_vibration/motor_vibration_wizard.hpp` (new)
- `src/common/marlin_server_types/fsm/motor_vibration_phases.hpp` (new)
