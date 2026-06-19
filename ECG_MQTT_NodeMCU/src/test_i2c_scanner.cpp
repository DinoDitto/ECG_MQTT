/*
 * test_i2c_scanner.cpp — 第 1 步：找出 LCD 的 I2C 位址
 *
 * 目的：掃描 I2C 匯流排，印出接在 SDA(GPIO21)/SCL(GPIO22) 上的裝置位址。
 *       LCD 常見位址為 0x27 或 0x3F。
 *
 * 使用：PlatformIO 側邊欄選 env:esp32_i2c → Upload → Monitor(115200)。
 *       記下印出的位址，再把 main.cpp 的
 *       LiquidCrystal_I2C lcd(0xXX, 16, 2) 改成正確位址。
 */

#include <Arduino.h>
#include <Wire.h>

void setup() {
  Serial.begin(115200);
  delay(500);
  Wire.begin(21, 22);  // SDA=GPIO21, SCL=GPIO22 (ESP32)
  Serial.println("\n=== I2C Scanner 開始掃描 ===");
}

void loop() {
  byte count = 0;
  for (byte addr = 1; addr < 127; addr++) {
    Wire.beginTransmission(addr);
    if (Wire.endTransmission() == 0) {
      Serial.print("找到 I2C 裝置，位址 0x");
      if (addr < 16) Serial.print("0");
      Serial.println(addr, HEX);
      count++;
    }
  }
  if (count == 0) Serial.println("找不到任何 I2C 裝置，請檢查 LCD 接線（VCC/GND/SDA/SCL）。");
  else { Serial.print("共 "); Serial.print(count); Serial.println(" 個裝置。"); }
  Serial.println("---");
  delay(3000);
}
