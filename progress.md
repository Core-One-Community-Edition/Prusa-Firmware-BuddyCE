# CoreXY Motor-Specific Shaper Optimizer - Progress

## Status: Analysis Complete

### Completed
- Full analysis of input_shaper_config.hpp (CoreOne defaults: X=MZV@60, Y=MZV@48)
- Full analysis of input_shaper.cpp (CoreXY dual-shaper path, motor A/B command composition)
- Full analysis of M958.cpp fit_shaper() (lines 1116-1195), remaining_vibrations() (lines 1044-1054), smoothing() (lines 1065-1084)
- Mathematical analysis of motor suppression coverage, coupled transfer function, search space explosion

### Key Findings
1. Current defaults only partially cover motor A at 45Hz (Y shaper at 48Hz gives ~10× vs 20×; X shaper at 60Hz gives nothing at 45Hz)
2. The coupled optimizer needs direction-dependent VR computation, not just two independent PSDs — but max(RV_X, RV_Y) is a safe conservative approximation
3. Full coupled search = 175× computation cost — impractical on MCU
4. Smoothing bounds can be directly reused from existing code
5. Recommended MVP: motor-aware dual-PSD scoring with ~50 LOC change

### Output
- Detailed tradeoff analysis written to: /tmp/pi-subagents-uid-1000/outputs/scout_tradeoffs
