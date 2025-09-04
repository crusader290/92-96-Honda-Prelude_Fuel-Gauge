# 🛠️ Honda Prelude Fuel Gauge Tap (Arduino Nano + OLED)

This project is an **Arduino Nano V3-based digital fuel gauge** that taps into the **fuel sender line** of a 1993 Honda Prelude (OBD1-era).  
It uses a **47 kΩ / 22 kΩ resistor divider** and a **100 nF capacitor** for noise suppression, then displays the **fuel level** on a **128×64 SH1106 OLED** in a clean, functional style.

---

## 🚗 What It Does
- Reads the **fuel sender line voltage** safely from 12 V → 5 V range.  
- Converts readings into a **fuel % (0–100%)**, scaled like the **Honda factory gauge** (E → H → F).  
- Displays:
  - **Large %** (rounded to 10s for stability)  
  - **Smooth fuel bar graph**  
  - **ADC value** (top-right, raw 0–1023)  
- Logs **hourly averaged ADC values** to EEPROM (24-slot ring buffer).  

---

## 📟 Features
- ✅ **10-sample averaging** + hardware filter → stable readings.  
- ✅ **¼-second updates** (no flicker).  
- ✅ **EEPROM calibration anchors** for **Empty (E)**, **Half (H)**, and **Full (F)**.  
- ✅ Each anchor can be **set, cleared, or defaulted individually**.  
- ✅ **Fallback defaults** use typical Honda values:
  - E ≈ 24 Ω, H ≈ 152 Ω, F ≈ 277 Ω  
  - Assumes `R_PULLUP = 120 Ω` and `Vsupply = 12.5 V`.  

---

## 🔌 Wiring
- Arduino **GND** must be tied to **vehicle ground**.  
- Optional: add a **1 kΩ series resistor** into A0 and a **5.1 V TVS/zener** for protection.  

---

## 🖥️ Serial Commands
Use the Arduino Serial Monitor to interact:

| Command | Action |
|---------|--------|
| `e`     | Set **Empty (E)** to current sender line (mV) and save to EEPROM |
| `h`     | Set **Half (H)** to current sender line (mV) and save |
| `f`     | Set **Full (F)** to current sender line (mV) and save |
| `!e`    | Clear **E** anchor (reverts to default, save) |
| `!h`    | Clear **H** anchor (reverts to default, save) |
| `!f`    | Clear **F** anchor (reverts to default, save) |
| `d`     | Re-derive **any unset anchors** from defaults (keep user-set) |
| `p`     | Print current active anchors and user-set flags |
| `O`     | Print **derived defaults** (E/H/F in mV from constants) |

---

## 📊 Display
- **Center:** Fuel % (rounded to nearest 10%)  
- **Bar Graph:** Smooth fill proportional to fuel %  
- **Bottom:** `E` and `F` labels  
- **Top-right:** ADC raw value (0–1023)  

---

## 🗂️ EEPROM
- Stores **anchors** (E/H/F mV values + flags).  
- Stores **24-hour rolling log** of averaged ADC readings.  

---

## ⚙️ Notes
- Defaults assume: `R_PULLUP = 120 Ω`, `Vsupply = 12.5 V`.  
- Calibrate in-car for accuracy: set **E** when nearly empty, **H** around half tank, and **F** when brimmed.  
- You may set any combination (e.g., only `f` to force Full = 100%).  
- Safe high-impedance tap — does **not affect cluster operation**.

---

## 📸 Example Output (Serial)

ADC=265  Line=4060 mV  Fuel=20% Anchors (mV): E=2080 H=6990 F=8710 User-set flags: E - F

---
