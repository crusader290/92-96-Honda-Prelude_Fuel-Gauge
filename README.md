# Honda Prelude (BA8) Fuel Gauge Project – Nano R4

This project uses an **Arduino Nano R4** to read the factory fuel sender in a 1993 Honda Prelude (BA8, F22A1) and display the fuel level on an OLED.

---

## ⚡ Hardware Overview

- **Board:** Arduino Nano R4 (Renesas RA4M1, 14-bit ADC)
- **ADC Range:** 0–5 V (0–16383 counts)  
  > Important: `analogReadResolution(14);` must be set in `setup()` to enable 14-bit reads.
- **Fuel Sender Source:** Yellow/Green wire between cluster and tank sender
- **Ground Reference:** Black wire (chassis ground)
- **Resistor Divider:** 47k (top) / 10k (bottom)  
  - Ratio ≈ 0.175  
  - Keeps the ~0.7–2.1 V sender signal safely in range  
  - Feeds into Nano R4 **A0** pin (5 V tolerant)

---

## 📈 Calibration (14-bit counts)

Measured using the 47k/10k divider on Nano R4, sender range ~0.7–2.1 V.  
Converted into ADC counts (0–16383):

| Liters | ADC Counts |
|--------|------------|
| 0      | 410        |
| 15     | 780        |
| 30     | 1040       |
| 45     | 1135       |
| 60     | 1187       |

Default calibration table in code:

```cpp
cal.points[0] = {  410,  0, true};   // Empty
cal.points[1] = {  780, 15, true};   // 1/4
cal.points[2] = { 1040, 30, true};   // 1/2
cal.points[3] = { 1135, 45, true};   // 3/4
cal.points[4] = { 1187, 60, true};   // Full

> EEPROM stores calibration. If invalid, defaults above are loaded.




---

🛠 Wiring

Tank Connector (rear, at pump/sender):

Yellow/Green → Sender signal → 47k/10k divider → A0

Black → Ground → Divider + Nano GND


Cluster Connector (front, behind dash):

Yellow/Green → Sender signal (same run as tank)

Black → Ground

Yellow/Black → 12 V IGN feed (⚠️ do NOT use for ADC)


You may tap either the tank plug or the cluster harness (easier if you’re already behind the dash).


---

🔧 Code Notes

1. Force 14-bit ADC mode:

analogReadResolution(14);   // Nano R4, 0–16383 counts


2. Use averaged ADC reads for stability:

long sum = 0;
for (int i = 0; i < 16; i++) sum += analogRead(A0);
int adcValue = sum / 16;


3. Clamp readings:

adcValue = constrain(adcValue, 0, 16383);




---

📟 Display

OLED is driven via U8g2, showing fuel level in liters.
Example output:

Fuel: 32 L


---

⚠️ Safety

Don’t tap the +12 V IGN feed — it won’t show fuel level and may damage the Nano.

Always confirm wire color with a multimeter before tapping (voltage should vary with tank level).

Add a series resistor (1–2 kΩ) before A0 and/or a TVS diode for surge protection if desired.



---

✅ Status

ADC proven working (analogReadResolution(14) required).

Calibration tested for 47k/10k divider.

Default EEPROM setup included.

Safe tap points identified: Yellow/Green sender wire at tank or cluster.


---
