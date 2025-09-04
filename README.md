# 🛠️ Honda Prelude Fuel Gauge (Arduino Nano + OLED)

An **Arduino Nano V3-based digital fuel gauge** for the **1993 Honda Prelude (OBD1-era)**.  
This project safely taps the **fuel sender line** with a high-impedance divider and displays a **digital percentage + bar graph** on a **128×64 SH1106 OLED**.

---

## 🚗 What It Does
- Reads the **fuel sender line voltage** (12 V → scaled for ADC).  
- Models the cluster’s **bimetallic gauge coil (~60 Ω)** feeding the sender.  
- Converts line voltage into **fuel percentage** similar to Honda’s factory gauge.  
- Displays:
  - **Large %** (rounded to 10s for readability)  
  - **Smooth fuel bar graph**  
  - **ADC value** (top-right)  
- Logs **hourly averaged ADC readings** to EEPROM (24-slot rolling buffer).  

---

## 📟 Features
- ✅ **10-sample averaging** + optional **100 nF cap** → stable readings  
- ✅ **4 Hz update rate** (every 250 ms, no flicker)  
- ✅ **EEPROM calibration anchors**: Empty (E), Half (H), Full (F)  
- ✅ **H is optional** →  
  - Linear mapping between E and F if unset  
  - Piecewise curve (E→H, H→F) if set  
- ✅ **Target calibration (`tNN`)**: instantly map the current reading to NN% (e.g., `t85`)  
- ✅ **Non-blocking serial input**: commands won’t freeze the gauge if mistyped  
- ✅ **Fallback defaults** from Honda sender ranges (24/152/277 Ω through a 60 Ω coil at 12.5 V)  

---

## 🔌 Wiring
- Arduino **GND** must be tied to **vehicle ground**.  
- Optional: add **1 kΩ series resistor** on A0 + **5.1 V zener/TVS** for protection.  

---

## 🖥️ Serial Commands

| Command | Action |
|---------|--------|
| `e`     | Set **Empty (E)** anchor to current voltage (save) |
| `h`     | Set **Half (H)** anchor (optional) |
| `f`     | Set **Full (F)** anchor |
| `!e`    | Clear E → revert to default |
| `!h`    | Clear H → revert to default (linear mode resumes) |
| `!f`    | Clear F → revert to default |
| `d`     | Re-derive any **unset anchors** from defaults |
| `p`     | Print current anchors + user flags + H mode status |
| `O`     | Print **derived defaults** (coil model, 60 Ω, 12.5 V) |
| `tNN`   | **Target calibration**: map current voltage to NN% (e.g., `t85`) |

---

## 📊 Display
- **Center:** Fuel % (rounded to nearest 10%)  
- **Bar Graph:** Smooth proportional fill  
- **Bottom:** `E` and `F` labels  
- **Top-right:** Raw ADC value (0–1023)  

---

## 🗂️ EEPROM
- Stores **E/H/F anchors** + **flags** (which are user-set)  
- Stores **24-hour rolling log** of hourly ADC averages  

---

## ⚙️ Notes
- Defaults assume:  
  - Gauge coil: ~60 Ω  
  - Supply: ~12.5 V (key-on, engine off)  
  - Sender ranges: E ≈ 24 Ω, H ≈ 152 Ω, F ≈ 277 Ω  
- Real cars vary: coil ≈ 50–70 Ω, supply ~13.8 V when charging  
- Use `e` and `f` for minimal calibration, or add `h` for Honda-style non-linear curve  
- Use `tNN` for quick correction (e.g., `t85` to force current voltage to 85%)  
- **Non-blocking input**: mistyped commands (like lone `!`) are safely ignored  
- Tap is **high-impedance** → does **not affect the factory cluster gauge**  

---

## 📸 Example Serial Output

Prelude fuel sender tap via 47k/22k + 100nF. H is OPTIONAL (linear if unset). Commands: e/h/f set live; !e/!h/!f clear; d derive unset; p print; O defaults; tNN target %.

ADC=265  Line=4060 mV  Fuel=20% Anchors (mV): E=3571 H=8962 F=10274 User-set flags: E - F H mode: UNUSED (linear E<->F)

---
