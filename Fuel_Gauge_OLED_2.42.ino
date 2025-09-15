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
  uint16_t mV;
  uint8_t liters;
  bool isDefault;
};

#define MAX_CAL_POINTS 16
struct CalData {
  CalPoint points[MAX_CAL_POINTS];
  uint8_t count;
  uint8_t valid;
} cal;

const int EE_ADDR_CAL = 0;
uint32_t ADC_REF_mV = 3300UL;

// ---------- Helpers ----------
static inline uint16_t clamp16(uint32_t x){ return (x>65535UL)?65535U:(uint16_t)x; }
long readVcc_mV() { return 3300; }

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
void loadHondaDefaults() {
  cal.count = 5;
  cal.points[0] = {  620,  0, true};
  cal.points[1] = { 1180, 15, true};
  cal.points[2] = { 1570, 30, true};
  cal.points[3] = { 1720, 45, true};
  cal.points[4] = { 1800, 60, true};
  cal.valid = 0xA5;
  saveCal();
}

// ---------- Interpolation ----------
float litersFromLine(uint16_t v_line) {
  if (cal.count < 2) return 0;
  for (uint8_t i=0; i<cal.count-1; i++) {
    if (v_line >= cal.points[i].mV && v_line <= cal.points[i+1].mV) {
      float slope = (float)(cal.points[i+1].liters - cal.points[i].liters) /
                    (float)(cal.points[i+1].mV - cal.points[i].mV);
      return (v_line - cal.points[i].mV) * slope + cal.points[i].liters;
    }
  }
  if (v_line < cal.points[0].mV) return 0;
  if (v_line > cal.points[cal.count-1].mV) return TANK_LITERS;
  return 0;
}

// ---------- ADC (14-bit for Nano R4) ----------
uint16_t readSenderLine_mV(uint16_t &adc_out, uint16_t &vadc_out){
  const uint8_t N=10;
  uint32_t sum=0;
  for(uint8_t i=0;i<N;i++){ sum+=analogRead(A0); delay(2); }
  adc_out = (uint16_t)(sum/N);

  vadc_out = (uint32_t)adc_out * ADC_REF_mV / 16383UL;
  uint32_t v_line_mV = (uint32_t)((float)vadc_out / DIVIDER_RATIO);
  return clamp16(v_line_mV);
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

void drawFuelOLED(uint8_t fuelPercent, int liters, uint16_t adcAvg) {
  u8g2.clearBuffer();
  const int barX = 15, barY = 40, barW = 100, barH = 12;

  // Bar
  u8g2.drawFrame(barX, barY, barW, barH);
  int fillW = (barW * fuelPercent) / 100;
  u8g2.drawBox(barX, barY, fillW, barH);

  u8g2.setFont(u8g2_font_5x7_tr);

  int quarterX = barX + (barW * 1) / 4;
  int halfX    = barX + (barW * 2) / 4;
  int threeX   = barX + (barW * 3) / 4;

  // Labels + ticks
  u8g2.drawStr(barX - 8, barY - 6, "E");
  u8g2.drawLine(barX, barY - 6, barX, barY - 2);

  u8g2.drawStr(barX + barW + 2, barY - 6, "F");
  u8g2.drawLine(barX + barW, barY - 6, barX + barW, barY - 2);

  drawCenteredStr(quarterX, barY - 6, "1/4");
  u8g2.drawLine(quarterX, barY - 6, quarterX, barY - 2);

  drawCenteredStr(halfX, barY - 6, "1/2");
  u8g2.drawLine(halfX, barY - 6, halfX, barY - 2);

  drawCenteredStr(threeX, barY - 6, "3/4");
  u8g2.drawLine(threeX, barY - 6, threeX, barY - 2);

  // Percent
  char buf[16];
  sprintf(buf, "%d%%", fuelPercent);
  drawCenteredStr(halfX, 20, buf);

  // Liters (floored)
  sprintf(buf, "%d L", liters);
  drawCenteredStr(halfX, barY + barH + 10, buf);

  // Debug ADC
  sprintf(buf, "ADC:%u", adcAvg);
  u8g2.drawStr(80, 10, buf);

  // Low fuel blink
  if (fuelPercent <= 17 && ((millis()/500)%2==0)) {
    u8g2.drawXBMP(2, 2, 16, 16, fuelPump16x16_bits);
  }

  u8g2.sendBuffer();
}

// ---------- Setup / Loop ----------
unsigned long lastSerialPrint = 0;

void setup(){
  Serial.begin(115200);
  delay(200);  // wait 200 ms
  u8g2.begin();
  ADC_REF_mV = readVcc_mV();

  loadCal();
  if (cal.count < 2) {
    loadHondaDefaults();
  }
}

void loop(){
  static unsigned long lastUpdate=0;
  unsigned long now=millis();

  if (now-lastUpdate>=250){
    lastUpdate=now;

    uint16_t adcAvg, vadc_mV;
    uint16_t v_line = readSenderLine_mV(adcAvg, vadc_mV);

    float fLiters = litersFromLine(v_line);
    if (fLiters < 0) fLiters = 0;
    if (fLiters > TANK_LITERS) fLiters = TANK_LITERS;

    int liters = (int)fLiters; // floor
    uint8_t fuelPercent = (uint8_t)( (fLiters / TANK_LITERS) * 100.0f );
    if (fuelPercent > 100) fuelPercent = 100;

    drawFuelOLED(fuelPercent, liters, adcAvg);

    if (now - lastSerialPrint >= 1000) {
      lastSerialPrint = now;
      Serial.print(F("ADC=")); Serial.print(adcAvg);
      Serial.print(F(" (14-bit)  Vadc=")); Serial.print(vadc_mV);
      Serial.print(F(" mV  Vline=")); Serial.print(v_line);
      Serial.print(F(" mV  Fuel=")); Serial.print(fuelPercent);
      Serial.print(F("%  Liters=")); Serial.println(liters);
    }
  }
}
