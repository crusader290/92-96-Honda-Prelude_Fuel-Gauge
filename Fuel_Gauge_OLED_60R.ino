#include <EEPROM.h>
#include <U8glib.h>

// ---------------- OLED ----------------
U8GLIB_SH1106_128X64 u8g(U8G_I2C_OPT_NONE);

// ----------- Divider (tap) -----------
const uint32_t R_TOP    = 47000UL; // to sender line (from cluster)
const uint32_t R_BOTTOM = 22000UL; // to GND
#define ADC_REF_mV 5000UL          // Nano @ 5V reference

// -------- Cluster gauge coil model (defaults) --------
// Treat the cluster's fuel gauge as a series coil (~60 Ω) feeding the sender to ground.
#define R_SERIES_OHMS 60U      // Gauge coil effective series resistance (default model)
#define V_SUPPLY_mV   12500U   // Key-on engine-off; try 13800 for charging voltage

// -------- Honda's average sender resistance anchors --------
#define R_E_OHMS  24U   // Empty avg (16–32 Ω → ~24 Ω)
#define R_H_OHMS 152U   // Half  avg (116–188 Ω → ~152 Ω)
#define R_F_OHMS 277U   // Full  avg (239–314 Ω → ~277 Ω)

// ---- Calibration/anchors in EEPROM ----
// flags bitmask: bit0=E user-set, bit1=H user-set, bit2=F user-set
struct CalData {
  uint16_t E_mV;  // line voltage anchor at Empty
  uint16_t H_mV;  // line voltage anchor at Half
  uint16_t F_mV;  // line voltage anchor at Full
  uint8_t  flags; // bits 0..2 indicate which anchors are user-set
  uint8_t  valid; // 0xA5 when struct valid
} cal;

const int EE_ADDR_CAL = 0;

// ---- Hourly ADC logging (24 entries) ----
const unsigned long LOG_INTERVAL_MS = 3600000UL; // 1 hour
unsigned long lastLog = 0;
uint8_t logIndex = 0;                             // RAM-only ring index
const int EE_ADDR_LOG = sizeof(CalData);          // logs after cal block

// ---- Heartbeat (blinks at 4 Hz) ----
bool heartbeat = false;

// ---------- Helpers ----------
static inline uint16_t clamp16(uint32_t x){ return (x>65535UL)?65535U:(uint16_t)x; }

// Default anchor calculation using *coil model*:
// Vline = Vs * R_sender / (R_series + R_sender)
uint16_t ohmsToLine_mV(uint16_t R_ohms){
  uint32_t num=(uint32_t)V_SUPPLY_mV*(uint32_t)R_ohms;
  uint32_t den=(uint32_t)R_SERIES_OHMS + (uint32_t)R_ohms;
  return clamp16(num/den);
}

void setDefaultsForUnset() {
  if (!(cal.flags & 0x01)) cal.E_mV = ohmsToLine_mV(R_E_OHMS);
  if (!(cal.flags & 0x02)) cal.H_mV = ohmsToLine_mV(R_H_OHMS);
  if (!(cal.flags & 0x04)) cal.F_mV = ohmsToLine_mV(R_F_OHMS);
}

void saveCal(){ EEPROM.put(EE_ADDR_CAL, cal); }
void loadCal(){
  EEPROM.get(EE_ADDR_CAL, cal);
  if (cal.valid != 0xA5) {
    cal.flags = 0;
    setDefaultsForUnset();
    cal.valid = 0xA5;
    saveCal();
  } else {
    // Keep user-set anchors; refresh unset from current defaults each boot
    setDefaultsForUnset();
  }
}

// --- Non-blocking serial helpers ---
bool readNextCharWithTimeout(char &out, unsigned long timeout_ms = 150) {
  unsigned long start = millis();
  while (millis() - start < timeout_ms) {
    if (Serial.available()) {
      out = Serial.read();
      return true;
    }
  }
  return false; // timed out
}

void flushLineEndings() {
  // eat CR/LF and spaces
  while (Serial.available()) {
    char c = Serial.peek();
    if (c == '\r' || c == '\n' || c == ' ') { Serial.read(); }
    else break;
  }
}

// Clear one anchor back to default and persist
void clearAnchor(char which){
  if (which=='e' || which=='E'){
    cal.flags &= ~0x01;
    cal.E_mV = ohmsToLine_mV(R_E_OHMS);
    Serial.print(F("Cleared E -> default ")); Serial.print(cal.E_mV); Serial.println(F(" mV (saved)"));
  } else if (which=='h' || which=='H'){
    cal.flags &= ~0x02;
    cal.H_mV = ohmsToLine_mV(R_H_OHMS);
    Serial.print(F("Cleared H -> default ")); Serial.print(cal.H_mV); Serial.println(F(" mV (saved)"));
  } else if (which=='f' || which=='F'){
    cal.flags &= ~0x04;
    cal.F_mV = ohmsToLine_mV(R_F_OHMS);
    Serial.print(F("Cleared F -> default ")); Serial.print(cal.F_mV); Serial.println(F(" mV (saved)"));
  } else {
    Serial.println(F("Unknown clear target; use !e, !h, or !f"));
    return;
  }
  cal.valid = 0xA5;
  saveCal();
}

void logADCHourly(uint16_t adcAvg){
  int addr = EE_ADDR_LOG + (logIndex*sizeof(uint16_t));
  EEPROM.update(addr,   (uint8_t)(adcAvg & 0xFF));
  EEPROM.update(addr+1, (uint8_t)(adcAvg >> 8));
  logIndex = (logIndex+1)%24;
}

uint16_t readSenderLine_mV(uint16_t &adc_out){
  const uint8_t N=10; // 10-sample average; add 100nF at A0 in hardware
  uint32_t sum=0;
  for(uint8_t i=0;i<N;i++){ sum+=analogRead(A0); delay(2); }
  adc_out = (uint16_t)(sum/N);

  uint32_t Vadc_mV  = (uint32_t)adc_out * ADC_REF_mV / 1023UL;
  uint32_t Vline_mV = Vadc_mV * (R_TOP+R_BOTTOM) / R_BOTTOM; // ≈ ×3.136
  return clamp16(Vline_mV);
}

// ---------- Mapping: H optional ----------
// If H (bit1) is NOT user-set, use linear E<->F.
// If H is user-set, use piecewise E->H (0..50%) and H->F (50..100%).
uint8_t fuelPercentFromLine(uint16_t v_mV){
  int32_t E=cal.E_mV, H=cal.H_mV, F=cal.F_mV;
  bool H_active = (cal.flags & 0x02);
  bool asc = (F >= E);

  // Linear 2-point mapping if H not active
  if (!H_active) {
    int32_t span = (int32_t)F - (int32_t)E;
    if (span == 0) return 0;
    int32_t pct = ((int32_t)v_mV - E) * 100L / span;
    if (pct < 0) pct = 0;
    if (pct > 100) pct = 100;
    return (uint8_t)pct;
  }

  // Ensure H sits between E and F
  if (asc){ if(H<E)H=E; if(H>F)H=F; }
  else     { if(H>E)H=E; if(H<F)H=F; }

  int32_t pct=0;
  if (asc){
    if (v_mV<=E) pct=0;
    else if (v_mV>=F) pct=100;
    else if (v_mV<=H){
      int32_t span=H-E; if(!span) span=1;
      pct=((int32_t)v_mV-E)*50L/span;
    }else{
      int32_t span=F-H; if(!span) span=1;
      pct=50+((int32_t)v_mV-H)*50L/span;
    }
  }else{
    if (v_mV>=E) pct=0;
    else if (v_mV<=F) pct=100;
    else if (v_mV>=H){
      int32_t span=E-H; if(!span) span=1;
      pct=(E-(int32_t)v_mV)*50L/span;
    }else{
      int32_t span=H-F; if(!span) span=1;
      pct=50+(H-(int32_t)v_mV)*50L/span;
    }
  }
  if (pct<0) pct=0; if (pct>100) pct=100;
  return (uint8_t)pct;
}

// -------- Fit current reading to target % (H optional aware) --------
// - If H active: target<=50 adjusts H; target>50 adjusts F.
// - If H inactive: adjust F so linear E<->F passes through target at current V.
void fitCurrentToTarget(uint16_t v_line_mV, uint8_t targetPct) {
  if (targetPct > 100) targetPct = 100;
  int32_t E = cal.E_mV, H = cal.H_mV, F = cal.F_mV;
  bool H_active = (cal.flags & 0x02);
  bool asc = (F >= E);

  if (!H_active) {
    if (targetPct == 0) targetPct = 1; // avoid div0
    int32_t v = v_line_mV;
    if ((asc && v <= E) || (!asc && v >= E)) v = E + (asc ? 1 : -1);
    int32_t Fnew = E + ((int32_t)(v - E) * 100L) / targetPct;
    if (asc) { if (Fnew <= E + 1) Fnew = E + 1; }
    else     { if (Fnew >= E - 1) Fnew = E - 1; }
    cal.F_mV = (uint16_t)clamp16(Fnew);
    cal.flags |= 0x04; // F user-set
    Serial.print(F("Fitted current to ")); Serial.print(targetPct); Serial.println(F("% via F (linear mode, saved)"));
  } else {
    if (targetPct <= 50) {
      int32_t v = v_line_mV;
      if (asc) { if (v <= E) v = E + 1; } else { if (v >= E) v = E - 1; }
      if (targetPct == 0) targetPct = 1;
      int32_t Hnew = E + ((int32_t)(v - E) * 50L) / targetPct;
      if (asc) { if (Hnew < E + 1) Hnew = E + 1; if (Hnew > F - 1) Hnew = F - 1; }
      else     { if (Hnew > E - 1) Hnew = E - 1; if (Hnew < F + 1) Hnew = F + 1; }
      cal.H_mV = (uint16_t)clamp16(Hnew);
      cal.flags |= 0x02; // H user-set
      Serial.print(F("Fitted current to ")); Serial.print(targetPct); Serial.println(F("% via H (piecewise, saved)"));
    } else {
      uint8_t t = targetPct - 50; if (t == 0) t = 1;
      int32_t v = v_line_mV;
      if (asc) { if (v <= H) v = H + 1; } else { if (v >= H) v = H - 1; }
      int32_t Fnew = H + ((int32_t)(v - H) * 50L) / t;
      if (asc) { if (Fnew < H + 1) Fnew = H + 1; if (Fnew < E + 2) Fnew = E + 2; }
      else     { if (Fnew > H - 1) Fnew = H - 1; if (Fnew > E - 2) Fnew = E - 2; }
      cal.F_mV = (uint16_t)clamp16(Fnew);
      cal.flags |= 0x04; // F user-set
      Serial.print(F("Fitted current to ")); Serial.print(targetPct); Serial.println(F("% via F (piecewise, saved)"));
    }
  }
  cal.valid = 0xA5;
  saveCal();
}

// ----------------- OLED -----------------
void drawFuelOLED(uint8_t fuelSmooth, uint16_t adcVal){
  // Snap displayed % to nearest 10
  uint8_t fuel10 = (uint8_t)(((uint16_t)fuelSmooth + 5) / 10) * 10;
  if (fuel10>100) fuel10=100;

  u8g.firstPage();
  do{
    u8g.setColorIndex(0); u8g.drawBox(0,0,128,64); u8g.setColorIndex(1);

    // % text (snapped to 10s)
    char buf[8];
    sprintf(buf, "%d%%", fuel10);
    u8g.setFont(u8g_font_8x13B);
    int textX=(128-u8g.getStrWidth(buf))/2;
    int textY=20;
    u8g.drawStr(textX, textY, buf);

    // Fuel bar (continuous / smooth)
    int barHeight=12;
    int barX=10;
    int barWfull=128-2*barX;
    int barY=40;
    int barW=map(fuelSmooth, 0, 100, 0, barWfull);
    u8g.drawFrame(barX, barY, barWfull, barHeight);
    u8g.drawBox(barX, barY, barW, barHeight);

    // E / F labels
    u8g.setFont(u8g_font_6x10);
    u8g.drawStr(4,  barY+barHeight+10, "E");
    u8g.drawStr(124, barY+barHeight+10, "F");

    // ADC value top-right
    char adcBuf[10];
    sprintf(adcBuf, "%u", adcVal);
    int adcX=128-u8g.getStrWidth(adcBuf)-2;
    u8g.drawStr(adcX, 10, adcBuf);

    // Heartbeat dot (blinks at 4 Hz) in top-left
    if (heartbeat) {
      u8g.drawStr(2, 10, "*");
    }

  }while(u8g.nextPage());
}

// ----------- Printers -----------
void printStatus() {
  Serial.print(F("Anchors (mV): E=")); Serial.print(cal.E_mV);
  Serial.print(F(" H=")); Serial.print(cal.H_mV);
  Serial.print(F(" F=")); Serial.println(cal.F_mV);
  Serial.print(F("User-set flags: "));
  Serial.print((cal.flags & 0x01) ? F("E ") : F("- "));
  Serial.print((cal.flags & 0x02) ? F("H ") : F("- "));
  Serial.println((cal.flags & 0x04) ? F("F ") : F("- "));
  Serial.println((cal.flags & 0x02) ? F("H mode: ACTIVE (piecewise)") : F("H mode: UNUSED (linear E<->F)"));
}

void printDefaults() {
  uint16_t Edef = ohmsToLine_mV(R_E_OHMS);
  uint16_t Hdef = ohmsToLine_mV(R_H_OHMS);
  uint16_t Fdef = ohmsToLine_mV(R_F_OHMS);
  Serial.println(F("=== Derived DEFAULTS (coil model) ==="));
  Serial.print(F("R_SERIES=")); Serial.print(R_SERIES_OHMS); Serial.print(F(" Ω, Vsupply="));
  Serial.print(V_SUPPLY_mV); Serial.println(F(" mV"));
  Serial.print(F("E_def=")); Serial.print(Edef); Serial.print(F(" mV  (R=")); Serial.print(R_E_OHMS); Serial.println(F(" Ω)"));
  Serial.print(F("H_def=")); Serial.print(Hdef); Serial.print(F(" mV  (R=")); Serial.print(R_H_OHMS); Serial.println(F(" Ω)"));
  Serial.print(F("F_def=")); Serial.print(Fdef); Serial.print(F(" mV  (R=")); Serial.print(R_F_OHMS); Serial.println(F(" Ω)"));
}

// -------------- Setup / Loop --------------
void setup(){
  Serial.begin(115200);
  loadCal();
  Serial.println(F("Prelude fuel sender tap via 47k/22k + 100nF. H is OPTIONAL (linear if unset)."));
  Serial.println(F("Commands: e/h/f set live; !e/!h/!f clear; d derive unset; p print; O defaults; tNN target %."));
  printStatus();
}

void loop(){
  static unsigned long lastUpdate=0;
  unsigned long now=millis();

  if (now-lastUpdate>=250){
    lastUpdate=now;

    // ---- Non-blocking serial command handling ----
    flushLineEndings();
    while (Serial.available()) {
      char c = Serial.read();
      if (c == '\r' || c == '\n' || c == ' ') continue; // ignore whitespace

      if (c=='e'||c=='E'){
        uint16_t d; cal.E_mV=readSenderLine_mV(d); cal.flags|=0x01; cal.valid=0xA5; saveCal();
        Serial.print(F("EMPTY set @ ")); Serial.print(cal.E_mV); Serial.println(F(" mV (saved)"));
      } else if (c=='h'||c=='H'){
        uint16_t d; cal.H_mV=readSenderLine_mV(d); cal.flags|=0x02; cal.valid=0xA5; saveCal();
        Serial.print(F("HALF  set @ ")); Serial.print(cal.H_mV); Serial.println(F(" mV (saved)"));
      } else if (c=='f'||c=='F'){
        uint16_t d; cal.F_mV=readSenderLine_mV(d); cal.flags|=0x04; cal.valid=0xA5; saveCal();
        Serial.print(F("FULL  set @ ")); Serial.print(cal.F_mV); Serial.println(F(" mV (saved)"));
      } else if (c=='!'){ // clear one anchor with timeout
        char t;
        if (readNextCharWithTimeout(t)) {
          clearAnchor(t);
        } else {
          Serial.println(F("Ignored lone '!' (no e/h/f)"));
        }
      } else if (c=='d'||c=='D'){
        setDefaultsForUnset(); cal.valid=0xA5; saveCal();
        Serial.println(F("Re-derived defaults for any UNSET anchors (kept user-set)."));
      } else if (c=='p'||c=='P'){
        printStatus();
      } else if (c=='O'){ // capital 'O'
        printDefaults();
      } else if (c=='t' || c=='T'){ // target NN% with timeout
        char d1, d2;
        int target = -1;
        if (readNextCharWithTimeout(d1)) {
          if (d1 >= '0' && d1 <= '9') {
            if (readNextCharWithTimeout(d2, 50) && (d2 >= '0' && d2 <= '9')) {
              target = (d1 - '0')*10 + (d2 - '0');
            } else {
              target = (d1 - '0');
            }
          }
        }
        if (target >= 0) {
          if (target > 100) target = 100;
          uint16_t adcAvg; uint16_t v_line = readSenderLine_mV(adcAvg);
          fitCurrentToTarget(v_line, (uint8_t)target);
          Serial.print(F("Current mapped to ")); Serial.print(target); Serial.println(F("% at this voltage."));
        } else {
          Serial.println(F("tNN usage: send 't85' to map current to 85%."));
        }
      } else {
        // Unknown command; ignore
      }
      flushLineEndings();
    }

    // Read & display
    uint16_t adcAvg;
    uint16_t v_line=readSenderLine_mV(adcAvg);
    uint8_t fuelSmooth=fuelPercentFromLine(v_line);
    drawFuelOLED(fuelSmooth, adcAvg);

    // Toggle heartbeat each update (for blink)
    heartbeat = !heartbeat;

    // Debug line
    Serial.print(F("ADC=")); Serial.print(adcAvg);
    Serial.print(F("  Line=")); Serial.print(v_line); Serial.print(F(" mV  Fuel="));
    Serial.print(fuelSmooth); Serial.println(F("%"));
  }

  // Hourly logging
  if (now-lastLog>=LOG_INTERVAL_MS){
    lastLog=now;
    uint16_t adcAvg; (void)readSenderLine_mV(adcAvg);
    logADCHourly(adcAvg);
    Serial.print(F("Logged hourly ADC avg: ")); Serial.println(adcAvg);
  }
}
