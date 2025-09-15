Honda Prelude Fuel Gauge Replacement – Arduino Nano R4

A digital replacement for the 1990s Honda Prelude analog fuel gauge, running on an Arduino Nano R4 with a 2.42" OLED.
Reads the OEM fuel sender through a resistor divider, applies calibration, and displays liters, %, ADC, and a low-fuel warning.

✨ Features

📟 OLED Display

Fuel level % (top)

Liters (bottom center)

ADC raw value (top-right)

Blinking fuel pump icon <17% (~10 L)

⚡ Honda defaults built-in

Empty = ~0 L @ 620 mV

Half = ~30 L @ 1570 mV

Full = ~60 L @ 1800 mV

🛠️ Calibration System

User-set anchors [SET] saved in EEPROM (persist after reset)

Defaults [DEFAULT] always available for comparison

Linear interpolation between anchors

Honda curve supported with multiple anchors

🔒 Safe for Nano R4

12-bit ADC (0–4095)

3.3 V reference voltage

Voltage divider (47k / 10k) keeps line <3.3 V

🧩 Hardware

Arduino Nano R4 Minima / WiFi

2.42" OLED (SSD1309 / SSD1306 compatible)

Voltage divider:

R1 = 47kΩ (sender → A0)

R2 = 10kΩ (A0 → GND)

OEM Honda Prelude fuel sender wire tapped

⚠️ Ensure sender line voltage at A0 never exceeds 3.3 V.

💻 Commands
Calibration
e       Set EMPTY  (0 L)
h       Set HALF   (30 L)
f       Set FULL   (60 L)
lNN     Set current = NN liters (e.g. l10 = 10 L)

Clear (only clears [SET] anchors)
!e      Clear user-set Empty   (0 L)
!h      Clear user-set Half    (30 L)
!f      Clear user-set Full    (60 L)
!NN     Clear user-set at NN L (e.g. !10 = clear 10 L anchor)

Utility
p       Print all calibration points ([DEFAULT] + [SET])
x       Reset → load Honda defaults
?       Show help menu

🔧 Workflow

Reset defaults

> x
Calibration reset to Honda defaults.
0: 620 mV  = 0 L   [DEFAULT]
1: 1570 mV = 30 L  [DEFAULT]
2: 1800 mV = 60 L  [DEFAULT]


Add real anchors while driving

Near empty → e

Half tank → h

Full tank → f

Optional fine-tuning → l10, l15, l45, etc.

Check calibration

p → Lists all anchors

[SET] always overrides [DEFAULT] in calculations

Clear mistakes

Wrong anchor? → !NN (e.g. !10)

🚨 Notes

Needs at least 2 anchors for valid interpolation.

More anchors = more accurate Honda non-linear curve.

Defaults alone = “ballpark accurate,” especially mid/full.

Near empty is least accurate without calibration → recommended to set e at low fuel light.

Unplugged sender → may float ADC → add a pull-down resistor or code check for open circuit.

📜 License

MIT License — free to use, modify, and share.
