#include <EEPROM.h>
#include <Wire.h>
#include <U8g2lib.h>

// ---------------- OLED ----------------
U8G2_SSD1309_128X64_NONAME0_F_HW_I2C u8g2(U8G2_R0, /* reset=*/ U8X8_PIN_NONE);

// ----------- Divider (tap) -----------
const uint32_t R_TOP    = 47000UL;
const uint32_t R_BOTTOM = 10000UL;
const float DIVIDER_RATIO = (float)R_BOTTOM / (R_TOP + R_BOTTOM);

// -------- Tank Capacity -----------
#define TANK_LITERS 60

// -------- Calibration structures --------
struct CalPoint {
  uint16_t mV;     // sender voltage
  uint8_t liters;  // liters at that voltage
  bool isDefault;  // default vs user-set
};

#define MAX_CAL_POINTS 16
struct CalData {
  CalPoint points[MAX_CAL_POINTS];
  uint8_t count;
  uint8_t valid;
} cal;

const int EE_ADDR_CAL = 0;
uint32_t ADC_REF_mV = 3300UL; // Nano R4 ADC reference (3.3V)

// ---------- Helpers ----------
static inline uint16_t clamp16(uint32_t x){ return (x>65535UL)?65535U:(uint16_t)x; }
long readVcc_mV() { return 3300; } // fixed for Nano R4

// ---------- EEPROM ----------
void saveCal(){ EEPROM.put(EE_ADDR_CAL, cal); }
void loadCal(){
  EEPROM.get(EE_ADDR_CAL, cal);
  if (cal.valid != 0xA5) {
    cal.count = 0;
    cal.valid = 0xA5;
    saveCal();
  }
}

// ---------- Calibration ----------
void addCalibration(uint16_t mV, uint8_t liters) {
  // Check for existing user-set at this liter level
  for (uint8_t i=0; i<cal.count; i++) {
    if (cal.points[i].liters == liters && !cal.points[i].isDefault) {
      cal.points[i].mV = mV;  // overwrite voltage
      saveCal();
      Serial.print(F("Updated calibration: "));
      Serial.print(mV); Serial.print(F(" mV = "));
      Serial.print(liters); Serial.println(F(" L [SET] (replaced old)"));
      return;
    }
  }

  // If not found, add a new one
  if (cal.count < MAX_CAL_POINTS) {
    cal.points[cal.count].mV = mV;
    cal.points[cal.count].liters = liters;
    cal.points[cal.count].isDefault = false;
    cal.count++;
    // sort by voltage
    for (uint8_t i=0; i<cal.count; i++) {
      for (uint8_t j=i+1; j<cal.count; j++) {
        if (cal.points[j].mV < cal.points[i].mV) {
          CalPoint tmp = cal.points[i];
          cal.points[i] = cal.points[j];
          cal.points[j] = tmp;
        }
      }
    }
    saveCal();
    Serial.print(F("Added calibration: ")); 
    Serial.print(mV); Serial.print(F(" mV = "));
    Serial.print(liters); Serial.println(F(" L [SET]"));
  } else {
    Serial.println(F("Max calibration points reached."));
  }
}

void loadHondaDefaults() {
  cal.count = 3;

  cal.points[0] = {620, 0, true};   // Empty
  cal.points[1] = {1570, 30, true}; // Half
  cal.points[2] = {1800, 60, true}; // Full

  cal.valid = 0xA5;
  saveCal();
}

void resetCalibration() {
  loadHondaDefaults();   // wipes old data + loads defaults
  Serial.println(F("Calibration reset to Honda defaults."));
  printCalibration();    // prints the defaults right away
}

void clearAnchorByLiters(uint8_t liters) {
  bool found = false;
  for (uint8_t i=0; i<cal.count; i++) {
    if (cal.points[i].liters == liters && !cal.points[i].isDefault) {
      for (uint8_t j=i; j<cal.count-1; j++) {
        cal.points[j] = cal.points[j+1];
      }
      cal.count--;
      found = true;
      break;
    }
  }
  if (found) {
    saveCal();
    Serial.print(F("Cleared user-set anchor at ")); Serial.print(liters); Serial.println(F(" L"));
  } else {
    Serial.print(F("No user-set anchor found at ")); Serial.print(liters); Serial.println(F(" L"));
  }
}

void clearAnchorByTag(char tag) {
  if (tag == 'e' || tag == 'E') {
    clearAnchorByLiters(0);
  } else if (tag == 'h' || tag == 'H') {
    clearAnchorByLiters(30);
  } else if (tag == 'f' || tag == 'F') {
    clearAnchorByLiters(60);
  } else {
    Serial.println(F("Unknown clear target; use !e, !h, !f, or !NN"));
  }
}

// ---------- Interpolation ----------
float litersFromLine(uint16_t v_line) {
  if (cal.count < 2) return -1;

  for (uint8_t i=0; i<cal.count-1; i++) {
    if (v_line >= cal.points[i].mV && v_line <= cal.points[i+1].mV) {
      float slope = (float)(cal.points[i+1].liters - cal.points[i].liters) /
                    (float)(cal.points[i+1].mV - cal.points[i].mV);
      return (v_line - cal.points[i].mV) * slope + cal.points[i].liters;
    }
  }

  // Extrapolate below first
  if (v_line < cal.points[0].mV && cal.count >= 2) {
    float slope = (float)(cal.points[1].liters - cal.points[0].liters) /
                  (float)(cal.points[1].mV - cal.points[0].mV);
    return (v_line - cal.points[0].mV) * slope + cal.points[0].liters;
  }
  // Extrapolate above last
  if (v_line > cal.points[cal.count-1].mV && cal.count >= 2) {
    float slope = (float)(cal.points[cal.count-1].liters - cal.points[cal.count-2].liters) /
                  (float)(cal.points[cal.count-1].mV - cal.points[cal.count-2].mV);
    return (v_line - cal.points[cal.count-1].mV) * slope + cal.points[cal.count-1].liters;
  }
  return -1;
}

uint8_t fuelPercentFromLine(uint16_t v_line) {
  float liters = litersFromLine(v_line);
  if (liters < 0) liters = 0;
  if (liters > TANK_LITERS) liters = TANK_LITERS;
  return (uint8_t)((liters / TANK_LITERS) * 100.0f + 0.5f);
}

// ---------- ADC ----------
uint16_t readSenderLine_mV(uint16_t &adc_out, uint16_t &vadc_out){
  const uint8_t N=10;
  uint32_t sum=0;
  for(uint8_t i=0;i<N;i++){ sum+=analogRead(A0); delay(2); }
  adc_out = (uint16_t)(sum/N);

  vadc_out = (uint32_t)adc_out * ADC_REF_mV / 4095UL; // 12-bit ADC
  return vadc_out * (R_TOP+R_BOTTOM) / R_BOTTOM;
}

// ---------- OLED ----------
static const unsigned char fuelPump16x16_bits[] U8X8_PROGMEM = {
  0x00,0x00, 0xF8,0x07, 0x08,0x04, 0x08,0x04,
  0x18,0x06, 0xE8,0x05, 0x28,0x04, 0x30,0x06,
  0xE0,0x07, 0x20,0x04, 0xE0,0x07, 0xE0,0x07,
  0xE0,0x07, 0xE0,0x07, 0x00,0x00, 0x00,0x00
};

void drawCenteredStr(int cx, int y, const char* s) {
  int w = u8g2.getStrWidth(s);
  u8g2.drawStr(cx - (w / 2), y, s);
}

void drawFuelOLED(uint8_t fuelPercent, float liters, uint16_t adcAvg) {
  u8g2.clearBuffer();
  const int barX = 15, barY = 40, barW = 100, barH = 12;

  u8g2.drawFrame(barX, barY, barW, barH);
  int fillW = (barW * fuelPercent) / 100;
  u8g2.drawBox(barX, barY, fillW, barH);

  u8g2.setFont(u8g2_font_6x10_tr);
  u8g2.drawStr(barX - 10, barY + barH, "E");
  u8g2.drawStr(barX + barW + 4, barY + barH, "F");

  for (int i = 1; i < 4; i++) {
    int notchX = barX + (barW * i) / 4;
    u8g2.drawLine(notchX, barY - 4, notchX, barY);
  }

  const int halfX = barX + barW / 2;
  drawCenteredStr(halfX, barY - 6, "1/2");

  char buf[16];
  sprintf(buf, "%d%%", fuelPercent);
  drawCenteredStr(halfX, 20, buf);

  sprintf(buf, "%d L", (int)(liters + 0.5));
  drawCenteredStr(halfX, barY + barH + 10, buf);

  sprintf(buf, "ADC:%u", adcAvg);
  u8g2.drawStr(80, 10, buf);

  if (fuelPercent < 17 && ((millis()/500)%2==0)) {
    u8g2.drawXBMP(2, 2, 16, 16, fuelPump16x16_bits);
  }

  u8g2.sendBuffer();
}

// ---------- Menu ----------
void printCommandMenu() {
  Serial.println(F("---- Command Menu ----"));
  Serial.println(F(" e   = set Empty (0 L)"));
  Serial.println(F(" h   = set Half  (30 L)"));
  Serial.println(F(" f   = set Full  (60 L)"));
  Serial.println(F(" lNN = set current as NN liters"));
  Serial.println(F(" !e  = clear user-set Empty (0 L)"));
  Serial.println(F(" !h  = clear user-set Half  (30 L)"));
  Serial.println(F(" !f  = clear user-set Full  (60 L)"));
  Serial.println(F(" !NN = clear user-set anchor at NN liters"));
  Serial.println(F(" p   = print calibration points"));
  Serial.println(F(" x   = reset calibration (Honda defaults)"));
  Serial.println(F(" ?   = show this help menu"));
  Serial.println(F("------------------------"));
}

void printCalibration() {
  Serial.println(F("---- Calibration Points ----"));
  for (uint8_t i=0; i<cal.count; i++) {
    Serial.print(i); Serial.print(F(": "));
    Serial.print(cal.points[i].mV); Serial.print(F(" mV = "));
    Serial.print(cal.points[i].liters); Serial.print(F(" L  "));
    Serial.println(cal.points[i].isDefault ? F("[DEFAULT]") : F("[SET]"));
  }
  Serial.println(F("-----------------------------"));
}

// ---------- Setup / Loop ----------
unsigned long lastSerialPrint = 0;

void setup(){
  Serial.begin(115200);
  u8g2.begin();
  ADC_REF_mV = readVcc_mV();

  loadCal();
  if (cal.count < 2) {
    loadHondaDefaults(); // preload if nothing stored
  }
  printCalibration();
  printCommandMenu();
}

void loop(){
  static unsigned long lastUpdate=0;
  unsigned long now=millis();

  if (now-lastUpdate>=250){
    lastUpdate=now;

    if (Serial.available()) {
      String cmd = Serial.readStringUntil('\n');
      cmd.trim();

      if (cmd=="e"){ uint16_t a,v; uint16_t mv=readSenderLine_mV(a,v); addCalibration(mv,0); }
      else if (cmd=="h"){ uint16_t a,v; uint16_t mv=readSenderLine_mV(a,v); addCalibration(mv,30); }
      else if (cmd=="f"){ uint16_t a,v; uint16_t mv=readSenderLine_mV(a,v); addCalibration(mv,60); }
      else if (cmd.startsWith("l")) {
        int liters = cmd.substring(1).toInt();
        if (liters>=0 && liters<=TANK_LITERS) {
          uint16_t a,v; uint16_t mv=readSenderLine_mV(a,v);
          addCalibration(mv,liters);
        }
      }
      else if (cmd.startsWith("!")) {
        if (cmd.length() == 2 && isalpha(cmd[1])) {
          clearAnchorByTag(cmd[1]);
        } else {
          int liters = cmd.substring(1).toInt();
          if (liters >= 0 && liters <= TANK_LITERS) {
            clearAnchorByLiters(liters);
          } else {
            Serial.println(F("Invalid clear target, use !e, !h, !f, or !NN"));
          }
        }
      }
      else if (cmd=="p") printCalibration();
      else if (cmd=="x") resetCalibration();
      else if (cmd=="?") printCommandMenu();
    }

    uint16_t adcAvg, vadc_mV;
    uint16_t v_line = readSenderLine_mV(adcAvg, vadc_mV);
    float liters = litersFromLine(v_line);
    if (liters<0) liters=0;
    if (liters>TANK_LITERS) liters=TANK_LITERS;
    uint8_t fuelPercent = (uint8_t)((liters / TANK_LITERS) * 100.0f + 0.5f);

    drawFuelOLED(fuelPercent, liters, adcAvg);

    if (now - lastSerialPrint >= 1000) {
      lastSerialPrint = now;
      Serial.print(F("ADC=")); Serial.print(adcAvg);
      Serial.print(F("  Vline=")); Serial.print(v_line);
      Serial.print(F(" mV  Fuel=")); Serial.print(fuelPercent);
      Serial.print(F("%  Liters=")); Serial.println((int)(liters+0.5));
    }
  }
}
