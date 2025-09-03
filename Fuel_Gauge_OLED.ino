#include <EEPROM.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>

#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 32
#define OLED_RESET    -1
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);

const int adcPin = A0;
const float R1 = 10000.0;
const float R2 = 5000.0;

// 7-point nonlinear fuel table
const int numPoints = 7;
float R_table[numPoints] = {16, 32, 116, 152, 188, 239, 314};
float F_table[numPoints] = {0, 5, 25, 50, 65, 85, 100};

const unsigned long logInterval = 3600000; // 1 hour
unsigned long lastLogTime = 0;
int hourIndex = 0; // 0-23

// Lookup function
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

// Draw horizontal bar for fuel %
void drawFuelBar(float fuelPercent) {
  int barWidth = map(fuelPercent, 0, 100, 0, SCREEN_WIDTH);
  display.fillRect(0, SCREEN_HEIGHT-8, barWidth, 8, SSD1306_WHITE);
}

void setup() {
  Serial.begin(115200);
  Serial.println("Fuel Decoder Started");

  if(!display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) {
    Serial.println("SSD1306 allocation failed");
    while(true);
  }
  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(0,0);
  display.println("Fuel Decoder");
  display.display();
  delay(1000);
}

void loop() {
  // --- Read ADC and convert to resistance ---
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
  display.clearDisplay();
  display.setCursor(0,0);
  display.setTextSize(1);
  display.println("Fuel Level:");
  display.setTextSize(2);
  display.setCursor(0,12);
  display.print(fuelPercent, 1);
  display.println("%");
  display.setTextSize(1);
  drawFuelBar(fuelPercent);
  display.display();

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
    if (hourIndex >= 24) hourIndex = 0; // wrap around
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
