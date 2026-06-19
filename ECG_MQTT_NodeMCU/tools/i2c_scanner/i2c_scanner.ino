/*
 * I2C Scanner — 確認 LCD 1602A 的 I2C 位址（對應 SDD §8.2）
 *
 * 使用方式：暫時把此檔複製到 src/ 取代 main.cpp 編譯燒錄，
 *           開啟序列埠監看（115200）讀出位址後，再把 main.cpp 的
 *           LiquidCrystal_I2C lcd(0xXX, 16, 2) 改成正確位址。
 *
 * 出廠常見位址：0x27 或 0x3F。
 */

#include <Wire.h>

void setup() {
  Serial.begin(115200);
  Wire.begin(21, 22);  // SDA=GPIO21, SCL=GPIO22 (ESP32)
  Serial.println("\nI2C Scanner 開始掃描...");
}

void loop() {
  byte count = 0;
  for (byte addr = 1; addr < 127; addr++) {
    Wire.beginTransmission(addr);
    if (Wire.endTransmission() == 0) {
      Serial.print("I2C device found at 0x");
      if (addr < 16) Serial.print("0");
      Serial.println(addr, HEX);
      count++;
    }
  }
  if (count == 0) Serial.println("找不到任何 I2C 裝置，請檢查接線。");
  else            Serial.print(count), Serial.println(" 個裝置。");
  Serial.println("---");
  delay(3000);
}
