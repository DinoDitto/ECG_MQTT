/*
 * test_ecg_read.cpp — 第 2 步：讀 AD8232 心電訊號
 *
 * 目的：確認 AD8232 有正常輸出。
 *   - 讀 GPIO36 的 ADC 值並印到序列埠
 *   - 偵測 LO+/LO-（導線脫落）
 *
 * 使用：
 *   1. 接好 AD8232（3.3V 供電！OUTPUT→36, LO+→34, LO-→35）
 *   2. 電極貼到身上
 *   3. env:esp32_ecg → Upload → Monitor(115200)
 *   4. 也可開 PlatformIO 的 Serial Plotter 看波形（Plotter 圖示）
 *
 * 判讀：
 *   - 電極沒貼好 → 印 "LEAD OFF"
 *   - 貼好後數值會隨心跳上下跳動（出現規律的尖峰 = R 波）
 */

#include <Arduino.h>

#define ECG_PIN   36
#define LO_PLUS   34
#define LO_MINUS  35

void setup() {
  Serial.begin(115200);
  pinMode(LO_PLUS, INPUT);
  pinMode(LO_MINUS, INPUT);
  analogSetAttenuation(ADC_11db);   // 0~3.3V 量程
  delay(500);
  Serial.println("\n=== AD8232 ECG 讀取測試 ===");
}

void loop() {
  if (digitalRead(LO_PLUS) == HIGH || digitalRead(LO_MINUS) == HIGH) {
    Serial.println("LEAD OFF - 導線脫落，請檢查電極貼附");
  } else {
    int raw = analogRead(ECG_PIN);   // 0~4095
    // Teleplot 格式：>名稱:數值  （在 Teleplot 面板會自動畫成波形）
    Serial.print(">ecg:");
    Serial.println(raw);
  }
  delay(10);   // 100 Hz
}
