#include <EEPROM.h>
#include <U8glib.h>

// ---------------- OLED ----------------
U8GLIB_SH1106_128X64 u8g(U8G_I2C_OPT_NONE);

// ----------- Divider (tap) -----------
const uint32_t R_TOP    = 47000UL; // to sender line (from cluster)
const uint32_t R_BOTTOM = 22000UL; // to GND

// -------- Cluster gauge coil model (defaults) --------
#define R_SERIES_OHMS 60U      // Gauge coil effective series resistance (default model)
#define V_SUPPLY_mV   12500U   // Key-on engine-off; try 13800 for charging voltage

// -------- Honda's average sender resistance anchors --------
#define R_E_OHMS  24U   // Empty avg (16–32 Ω → ~24 Ω)
#define R_H_OHMS 152U   // Half  avg (116–188 Ω → ~152 Ω)
#define R_F_OHMS 277U   // Full  avg (239–314 Ω → ~277 Ω)

// ---- Calibration/anchors in EEPROM ----
struct CalData {
  uint16_t E_mV;  
  uint16_t H_mV;  
  uint16_t F_mV;  
  uint8_t  flags; 
  uint8_t  valid; 
} cal;

const int EE_ADDR_CAL = 0;

// ---- Heartbeat (blinks at 4 Hz) ----
bool heartbeat = false;

// ---------- Helpers ----------
static inline uint16_t clamp16(uint32_t x){ return (x>65535UL)?65535U:(uint16_t)x; }

// --- Measure actual Vcc (in mV) using internal 1.1V reference ---
long readVcc_mV() {
  ADMUX = _BV(REFS0) | _BV(MUX3) | _BV(MUX2) | _BV(MUX1); 
  delay(2); 
  ADCSRA |= _BV(ADSC); 
  while (bit_is_set(ADCSRA, ADSC)); 
  uint16_t result = ADC;
  long vcc = 1125300L / result; 
  return vcc; 
}

uint32_t ADC_REF_mV = 5000UL; // updated dynamically

// Default anchor calculation using *coil model*
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
  return false;
}

void flushLineEndings() {
  while (Serial.available()) {
    char c = Serial.peek();
    if (c == '\r' || c == '\n' || c == ' ') { Serial.read(); }
    else break;
  }
}

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

// Reads ADC, returns line voltage (mV). Also outputs raw ADC and pin voltage.
uint16_t readSenderLine_mV(uint16_t &adc_out, uint16_t &vadc_out){
  const uint8_t N=10;
  uint32_t sum=0;
  for(uint8_t i=0;i<N;i++){ sum+=analogRead(A0); delay(2); }
  adc_out = (uint16_t)(sum/N);

  vadc_out = (uint32_t)adc_out * ADC_REF_mV / 1023UL;
  uint32_t Vline_mV = (uint32_t)vadc_out * (R_TOP+R_BOTTOM) / R_BOTTOM;
  return clamp16(Vline_mV);
}

// ---------- Mapping + Display ----------

uint8_t fuelPercentFromLine(uint16_t v_line) {
  // Handle optional H calibration
  if (cal.flags & 0x02) {
    // Piecewise: E->H, H->F
    if (v_line <= cal.H_mV) {
      int32_t num = (int32_t)(v_line - cal.E_mV) * 50;
      int32_t den = (int32_t)(cal.H_mV - cal.E_mV);
      if (den <= 0) return 0;
      return (uint8_t) constrain(num / den, 0, 50);
    } else {
      int32_t num = (int32_t)(v_line - cal.H_mV) * 50;
      int32_t den = (int32_t)(cal.F_mV - cal.H_mV);
      if (den <= 0) return 100;
      return (uint8_t) constrain(50 + num / den, 50, 100);
    }
  } else {
    // Linear E->F
    int32_t num = (int32_t)(v_line - cal.E_mV) * 100;
    int32_t den = (int32_t)(cal.F_mV - cal.E_mV);
    if (den <= 0) return 0;
    return (uint8_t) constrain(num / den, 0, 100);
  }
}

void drawFuelOLED(uint8_t fuelPercent, uint16_t adcAvg) {
  u8g.firstPage();
  do {
    // Title
    u8g.setFont(u8g_font_6x13B);
    u8g.drawStr(0, 10, "Fuel Gauge");

    // Bar graph
    int barWidth = map(fuelPercent, 0, 100, 0, 120);
    u8g.drawFrame(0, 20, 128, 20);
    u8g.drawBox(0, 20, barWidth, 20);

    // Percent text
    char buf[16];
    sprintf(buf, "%3d%%", fuelPercent);
    u8g.drawStr(0, 55, buf);

    // Raw ADC debug
    sprintf(buf, "ADC:%u", adcAvg);
    u8g.drawStr(64, 55, buf);

  } while (u8g.nextPage());
}

void printStatus() {
  Serial.println(F("---- Calibration Status ----"));
  Serial.print(F("E anchor = ")); Serial.print(cal.E_mV); Serial.println(F(" mV"));
  Serial.print(F("H anchor = ")); Serial.print(cal.H_mV); Serial.println(F(" mV"));
  Serial.print(F("F anchor = ")); Serial.print(cal.F_mV); Serial.println(F(" mV"));
  Serial.print(F("Flags = ")); Serial.println(cal.flags, BIN);
  Serial.print(F("Valid = ")); Serial.println(cal.valid, HEX);
  Serial.print(F("Current Vcc = ")); Serial.print(ADC_REF_mV); Serial.println(F(" mV"));
}

void printDefaults() {
  Serial.println(F("---- Default Model Anchors ----"));
  Serial.print(F("E (24Ω) = ")); Serial.print(ohmsToLine_mV(R_E_OHMS)); Serial.println(F(" mV"));
  Serial.print(F("H (152Ω) = ")); Serial.print(ohmsToLine_mV(R_H_OHMS)); Serial.println(F(" mV"));
  Serial.print(F("F (277Ω) = ")); Serial.print(ohmsToLine_mV(R_F_OHMS)); Serial.println(F(" mV"));
}

// Simulate targeting a % fuel (demo only)
void fitCurrentToTarget(uint8_t targetPercent) {
  Serial.print(F("Simulating target fuel = "));
  Serial.print(targetPercent);
  Serial.println(F("% (no physical output driven)"));
}

// -------------- Setup / Loop --------------
void setup(){
  Serial.begin(115200);

  // Measure and store initial Vcc
  ADC_REF_mV = readVcc_mV();
  Serial.print(F("Boot Vcc = "));
  Serial.print(ADC_REF_mV);
  Serial.println(F(" mV"));

  loadCal();
  Serial.println(F("Prelude fuel sender tap via 47k/22k + 100nF. H is OPTIONAL (linear if unset)."));
  Serial.println(F("Commands: e/h/f set live; !e/!h/!f clear; d derive unset; p print; O defaults; tNN target %; V show Vcc."));
  printStatus();
}

void loop(){
  static unsigned long lastUpdate=0;
  unsigned long now=millis();

  // Refresh measured Vcc once per second
  static unsigned long lastVccCheck=0;
  if (now - lastVccCheck >= 1000) {
    lastVccCheck = now;
    ADC_REF_mV = readVcc_mV();
  }

  if (now-lastUpdate>=250){
    lastUpdate=now;

    flushLineEndings();
    while (Serial.available()) {
      char c = Serial.read();
      if (c == '\r' || c == '\n' || c == ' ') continue;

      if (c=='e'||c=='E'){ 
        uint16_t adc,v; uint16_t vline=readSenderLine_mV(adc,v);
        cal.E_mV=vline; cal.flags|=0x01; saveCal();
        Serial.print(F("Set E anchor = ")); Serial.print(vline); Serial.println(F(" mV (saved)"));
      }
      else if (c=='h'||c=='H'){ 
        uint16_t adc,v; uint16_t vline=readSenderLine_mV(adc,v);
        cal.H_mV=vline; cal.flags|=0x02; saveCal();
        Serial.print(F("Set H anchor = ")); Serial.print(vline); Serial.println(F(" mV (saved)"));
      }
      else if (c=='f'||c=='F'){ 
        uint16_t adc,v; uint16_t vline=readSenderLine_mV(adc,v);
        cal.F_mV=vline; cal.flags|=0x04; saveCal();
        Serial.print(F("Set F anchor = ")); Serial.print(vline); Serial.println(F(" mV (saved)"));
      }
      else if (c=='!'){ char t; if (readNextCharWithTimeout(t)) clearAnchor(t); }
      else if (c=='d'||c=='D'){ setDefaultsForUnset(); cal.valid=0xA5; saveCal(); Serial.println(F("Re-derived defaults.")); }
      else if (c=='p'||c=='P'){ printStatus(); }
      else if (c=='O'){ printDefaults(); }
      else if (c=='t' || c=='T'){ 
        char d1,d2; 
        if (readNextCharWithTimeout(d1) && readNextCharWithTimeout(d2)) {
          uint8_t tgt=(d1-'0')*10+(d2-'0');
          fitCurrentToTarget(tgt);
        }
      } 
      else if (c=='v' || c=='V'){ 
        Serial.print(F("Measured Vcc = "));
        Serial.print(ADC_REF_mV);
        Serial.println(F(" mV"));
      }
      flushLineEndings();
    }

    uint16_t adcAvg, vadc_mV;
    uint16_t v_line=readSenderLine_mV(adcAvg, vadc_mV);
    uint8_t fuelSmooth=fuelPercentFromLine(v_line);
    drawFuelOLED(fuelSmooth, adcAvg);

    heartbeat = !heartbeat;

    Serial.print(F("ADC=")); Serial.print(adcAvg);
    Serial.print(F("  Vadc=")); Serial.print(vadc_mV);
    Serial.print(F(" mV  Line=")); Serial.print(v_line);
    Serial.print(F(" mV  Fuel=")); Serial.print(fuelSmooth);
    Serial.println(F("%"));
  }
}
