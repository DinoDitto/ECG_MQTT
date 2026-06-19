/*
 * test_blink.cpp — 第 0 步：最小燒錄驗證
 *
 * 目的：單獨確認「PlatformIO 能編譯 → 能燒進 ESP32 → 板子有反應」。
 *       不接任何外部元件，只用板載 LED 與序列埠。
 *
 * 使用：PlatformIO 側邊欄選 env:esp32_test → Upload，
 *       再開 Serial Monitor（115200）應每秒看到一行訊息。
 *
 * 確認成功後，改用 env:esp32dev 燒正式韌體 main.cpp。
 */

#include <Arduino.h>

// 多數 ESP32 DevKit 板載 LED 在 GPIO2（若你的板子沒反應，這顆 LED 可能不存在，
// 但序列埠訊息仍應正常出現，那也算成功）。
#define LED_PIN 2

void setup() {
  Serial.begin(115200);
  delay(500);
  pinMode(LED_PIN, OUTPUT);
  Serial.println("\n=== ESP32 最小測試啟動 ===");
  Serial.println("如果你看到這行，代表燒錄成功！");
}

void loop() {
  digitalWrite(LED_PIN, HIGH);
  Serial.println("LED ON  (板子運作中...)");
  delay(1000);
  digitalWrite(LED_PIN, LOW);
  Serial.println("LED OFF");
  delay(1000);
}
