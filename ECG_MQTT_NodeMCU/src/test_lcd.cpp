/*
 * test_lcd.cpp — 第 1 步（續）：確認 LCD 能正常顯示文字
 *
 * 目的：在位址 0x27 的 LCD 上顯示文字並每秒更新計數，
 *       確認顯示正常、順便調整背面的對比度旋鈕。
 *
 * 使用：PlatformIO 側邊欄選 env:esp32_lcd → Upload → 看 LCD 螢幕。
 *
 * 若背光亮但看不到字 → 用小螺絲起子轉 LCD 背面的藍色旋鈕（對比度）。
 */

#include <Arduino.h>
#include <Wire.h>
#include <LiquidCrystal_I2C.h>

LiquidCrystal_I2C lcd(0x27, 16, 2);   // 位址 0x27（與 main.cpp 一致）

void setup() {
  Serial.begin(115200);
  Wire.begin(21, 22);   // SDA=GPIO21, SCL=GPIO22
  lcd.init();
  lcd.backlight();
  lcd.setCursor(0, 0);
  lcd.print("ECG Monitor");
  lcd.setCursor(0, 1);
  lcd.print("LCD OK!");
  Serial.println("LCD 測試啟動，螢幕應顯示 ECG Monitor / LCD OK!");
}

void loop() {
  static int count = 0;
  lcd.setCursor(0, 1);
  lcd.print("LCD OK! ");
  lcd.print(count);
  lcd.print("   ");   // 補空白蓋掉殘字
  count++;
  delay(1000);
}
