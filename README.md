# 🛠️ Honda Prelude Fuel Gauge (Arduino Nano + OLED)

An **Arduino Nano V3-based digital fuel gauge** for the **1993 Honda Prelude (OBD1-era)**.  
This project taps the **fuel sender line** using a safe high-impedance divider and displays a **digital percentage + bar graph** on a **128×64 SH1106 OLED**.

---

## 🚗 What It Does
- Reads the **fuel sender line voltage** (12 V → scaled to safe ADC range).  
- Models the cluster’s **bimetallic gauge coil (~60 Ω)** feeding the sender.  
- Converts sender line voltage into **fuel percentage** like Honda’s factory gauge.  
- Displays:
  - **Large %** (rounded to 10s for readability)  
  - **Smooth fuel bar graph**  
  - **ADC value** (top-right)  
- Logs **hourly averaged ADC readings** to EEPROM (24-slot ring buffer).  

---

## 📟 Features
- ✅ **10-sample averaging** + optional 100 nF capacitor → stable readings.  
- ✅ **¼-second updates** (no flicker).  
- ✅ **EEPROM calibration anchors** for **Empty (E)**, **Half (H)**, and **Full (F)**.  
- ✅ Anchors can be **set, cleared, or defaulted individually**.  
- ✅ **Target calibration (`tNN`)**: instantly map the current voltage to a chosen % (e.g., `t85`).  
- ✅ **Fallback defaults** use Honda-typical values with 60 Ω gauge coil model.  

---

## 🔌 Wiring
- Arduino **GND** must be tied to **vehicle ground**.  
- Optional: add **1 kΩ series resistor** to A0 + **5.1 V TVS/zener** for protection.  

---

## 🖥️ Serial Commands

| Command | Action |
|---------|--------|
| `e`     | Set **Empty (E)** to current voltage (save to EEPROM) |
| `h`     | Set **Half (H)** to current voltage (save) |
| `f`     | Set **Full (F)** to current voltage (save) |
| `!e`    | Clear **E** → revert to default |
| `!h`    | Clear **H** → revert to default |
| `!f`    | Clear **F** → revert to default |
| `d`     | Re-derive **any unset anchors** from defaults |
| `p`     | Print current anchors + user-set flags |
| `O`     | Print **derived defaults** (coil model E/H/F in mV) |
| `tNN`   | **Target calibration**: map current reading to **NN%** (e.g., `t85`) |

---

## 📊 Display
- **Center:** Fuel % (rounded to nearest 10%)  
- **Bar Graph:** Smooth proportional fill  
- **Bottom:** `E` and `F` labels  
- **Top-right:** Raw ADC value (0–1023)  

---

## 🗂️ EEPROM
- Stores **anchors (E/H/F)** + **flags** for which are user-set.  
- Stores **24-hour rolling log** of averaged ADC readings.  

---

## ⚙️ Notes
- **Defaults** assume:  
  - Gauge coil: `~60 Ω`  
  - Supply: `12.5 V` (engine off)  
  - Sender ranges: **E ≈ 24 Ω**, **H ≈ 152 Ω**, **F ≈ 277 Ω**  
- In real cars, values drift with charging voltage (~13.8 V) and coil variation.  
- Use **`e`/`h`/`f`** for accurate calibration, or **`tNN`** for fast correction.  
- Safe high-impedance tap — **does not affect cluster operation**.  

---

## 📸 Example Output (Serial)

Prelude fuel sender tap via 47k/22k + 100nF. Calibration OPTIONAL. Set anchors individually: 'e','h','f'. Clear: '!e','!h','!f'. 'd' re-derive unset. 'p' print current anchors, 'O' print defaults (coil model). 'tNN' fit current reading to NN%.

ADC=265  Line=4060 mV  Fuel=20% Anchors (mV): E=3571 H=8962 F=10274 User-set flags: - - -

---
