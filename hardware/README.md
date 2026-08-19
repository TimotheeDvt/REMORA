![REMORA logo](Assets/logo.svg)
# REMORA Hardware
### Real-time Expressive Motion to Output Routing Audio

The staff-mounted enclosure that carries the ESP32-S2, MPU-9250, and battery. See the [top-level README](../README.md) for how this fits into the full system, and [firmware/README.md](../firmware/README.md) for the electronics wiring (I2C pins, charging circuit) that this case is built around.

## Contents

```
hardware/
├── Assets/                # Reference STL models for the ESP32 board and MPU chip
├── PRINTABLE/              # Exported STL/gcode ready to slice and print
├── case_vscode.scad        # Parametric casing model (OpenSCAD Customizer)
├── base_print.scad         # Utility to export a flat cutting/drilling pattern
└── calibration_stand.scad  # Stand used for the plugin's three-pose calibration procedure
```

## Enclosure

`case_vscode.scad` is a parametric OpenSCAD model of the staff-mounted enclosure for the ESP32-S2, MPU-9250, and a 2000 mAh LiPo battery, closed with a strap-and-screw fit. Customizer variables live at the top of the file; `render_mode` switches between rendering the cap, the base, both, or neither for faster iteration.

`base_print.scad` projects the base down to a flat pattern for cutting/drilling references.

Ready-to-slice output lives in `PRINTABLE/` (`BASE.stl`, `CAP.stl`).

## Battery

The SparkFun ESP32-S2 Thing+ has an onboard LiPo charge circuit with a JST-PH connector: connect the battery to the board and USB charges it directly, so the only cable the performer needs between sets is for charging - no separate charger required. Battery runtime under REMORA's actual 100 Hz sampling load has not been measured yet.
