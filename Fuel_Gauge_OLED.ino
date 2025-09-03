#include <EEPROM.h>
#include <Wire.h>
#include <U8glib.h>

// --- OLED SH1106 I2C ---
U8GLIB_SH1106_128X64 u8g(U8G_I2C_OPT_NONE);

// --- Fuel sender ---
const int adcPin = A0;
const float R1 = 10000.0; // voltage divider
const float R2 = 5000.0;

// --- Fuel lookup table (nonlinear) ---
const int numPoints = 7;
float R_table[numPoints] = {16, 32, 116, 152, 188, 239, 314};
float F_table[numPoints] = {0, 5, 25, 50, 65, 85, 100};

// --- EEPROM logging ---
const unsigned long logInterval = 3600000; // 1 hour
unsigned long lastLogTime = 0;
int hourIndex = 0;

// --- Lookup function ---
float lookupFuel(float R) {
  if (R <= R_table[0]) return F_table[0];
  if (R >= R_table[numPoints - 1]) return F_table[numPoints - 1];
  for (int i = 0; i < numPoints - 1; i++) {
    if (R >= R_table[i] && R <= R_table[i + 1]) {
      float fraction = (R - R_table[i]) / (R_table[i + 1] - R_table[i]);
      return F_table[i] + fraction * (F_table[i + 1] - F_table[i]);
    }
  }
  return 0;
}

// --- Draw fuel display ---
void drawFuel(float fuelPercent) {
  int fuelRounded = round(fuelPercent); // round for display
  u8g.firstPage();
  do {
    // Title
    u8g.setFont(u8g_font_6x10);
    u8g.drawStr(0, 12, "Fuel Decoder");

    // Numeric fuel %
    char buf[10];
    sprintf(buf, "Fuel: %d%%", fuelRounded);
    u8g.setFont(u8g_font_10x20);
    u8g.drawStr(0, 40, buf);

    // Horizontal bar
    int barWidth = map(fuelRounded, 0, 100, 0, 128);
    u8g.drawBox(0, 60, barWidth, 4); // 4 pixels tall
  } while(u8g.nextPage());
}

void setup() {
  Serial.begin(115200);
  Serial.println("Fuel Decoder SH1106 Initialized");
}

void loop() {
  // --- Read ADC and convert to resistance ---
  int adc = analogRead(adcPin);
  float V_adc = (adc / 1023.0) * 5.0;
  float R_sending = (V_adc * R1) / (5.0 - V_adc);

  // --- Lookup fuel %
  float fuelPercent = lookupFuel(R_sending);
  fuelPercent = constrain(fuelPercent, 0, 100);

  // --- Serial output (precise float) ---
  Serial.print("Fuel %: ");
  Serial.println(fuelPercent, 1);

  // --- Update OLED (rounded) ---
  drawFuel(fuelPercent);

  // --- Log hourly to EEPROM ---
  unsigned long currentMillis = millis();
  if (currentMillis - lastLogTime >= logInterval) {
    lastLogTime = currentMillis;

    byte fuelByte = (byte)fuelPercent;
    EEPROM.update(hourIndex, fuelByte);

    Serial.print("Logged hour ");
    Serial.print(hourIndex);
    Serial.print(": Fuel % = ");
    Serial.println(fuelByte);

    hourIndex++;
    if (hourIndex >= 24) hourIndex = 0;
  }

  // --- Serial command to dump 24-hour log ---
  if (Serial.available()) {
    char cmd = Serial.read();
    if (cmd == 'R' || cmd == 'r') {
      Serial.println("24-hour Fuel Log:");
      for (int i = 0; i < 24; i++) {
        byte val = EEPROM.read(i);
        Serial.print("Hour ");
        Serial.print(i);
        Serial.print(": ");
        Serial.println(val);
      }
      Serial.println("End of log");
    }
  }

  delay(1000);
}
