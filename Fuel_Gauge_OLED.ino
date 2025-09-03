#include <EEPROM.h>
#include <U8glib.h>

const int adcPin = A0;

// OLED setup
U8GLIB_SSD1306_128X64 u8g(U8G_I2C_OPT_NONE);

// Voltage divider
const float R1 = 10000.0;
const float R2 = 5000.0;

// Nonlinear fuel mapping
const int numPoints = 7;
float R_table[numPoints] = {16, 32, 116, 152, 188, 239, 314};
float F_table[numPoints] = {0, 5, 25, 50, 65, 85, 100};

// Logging
const unsigned long logInterval = 3600000; // 1 hour
unsigned long lastLogTime = 0;
int hourIndex = 0; // 0-23

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

// --- OLED drawing function ---
void drawFuelOLED(float fuelPercent) {
  u8g.firstPage();
  do {
    // Title
    u8g.setFont(u8g_font_6x10);
    u8g.drawStr(0, 12, "Fuel Level:");

    // Large % text
    u8g.setFont(u8g_font_10x20);
    char buf[8];
    sprintf(buf, "%.1f%%", fuelPercent);
    u8g.drawStr(0, 40, buf);

    // Bar outline
    u8g.drawFrame(0, 54, 128, 10);

    // Bar fill
    int barWidth = map((int)fuelPercent, 0, 100, 0, 128);
    u8g.drawBox(0, 54, barWidth, 10);

    // Labels E, 1/4, 1/2, 3/4, F
    u8g.setFont(u8g_font_6x10);
    u8g.drawStr(0, 64, "E");
    u8g.drawStr(30, 64, "1/4");
    u8g.drawStr(60, 64, "1/2");
    u8g.drawStr(90, 64, "3/4");
    u8g.drawStr(120, 64, "F");

    // Tick marks
    u8g.drawVLine(32, 54, 10);
    u8g.drawVLine(64, 54, 10);
    u8g.drawVLine(96, 54, 10);

  } while(u8g.nextPage());
}

// --- EEPROM logging function ---
void logFuelEEPROM(float fuelPercent) {
  byte fuelByte = (byte)fuelPercent;
  EEPROM.update(hourIndex, fuelByte);
  Serial.print("Logged hour ");
  Serial.print(hourIndex);
  Serial.print(": Fuel % = ");
  Serial.println(fuelByte);

  hourIndex++;
  if (hourIndex >= 24) hourIndex = 0; // wrap
}

void setup() {
  Serial.begin(115200);
  Serial.println("Fuel Decoder Started");
}

void loop() {
  // --- Read ADC and convert to sender resistance ---
  int adc = analogRead(adcPin);
  float V_adc = (adc / 1023.0) * 5.0;
  float R_sending = (V_adc * R1) / (5.0 - V_adc);

  // --- Lookup fuel %
  float fuelPercent = lookupFuel(R_sending);
  fuelPercent = constrain(fuelPercent, 0, 100);

  // --- Live Serial output ---
  Serial.print("Fuel %: ");
  Serial.println(fuelPercent, 1);

  // --- Update OLED ---
  drawFuelOLED(fuelPercent);

  // --- Hourly EEPROM log ---
  unsigned long currentMillis = millis();
  if (currentMillis - lastLogTime >= logInterval) {
    lastLogTime = currentMillis;
    logFuelEEPROM(fuelPercent);
  }

  // --- Serial dump of 24-hour log ---
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

  delay(1000); // 1s loop
}
