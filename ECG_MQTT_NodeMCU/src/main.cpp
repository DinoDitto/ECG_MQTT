/*
 * ECG MQTT 心電圖監測系統 — NodeMCU-32 (ESP32) 韌體
 * 對應設計文件 ECG_MQTT_sdd.md v1.0（硬體已升級為 ESP32）
 *
 * 功能：AD8232 ECG 擷取 → IIR 濾波 → 動態門檻波峰偵測 → 3 拍平均 BPM
 *       → MQTT 發送 + LCD 顯示 + PWM 被動蜂鳴器 + 雙按鈕控制
 */

#include <WiFi.h>
#include <PubSubClient.h>
#include <Wire.h>
#include <LiquidCrystal_I2C.h>

// ======================= 使用者設定（請依環境修改） =======================
const char* WIFI_SSID     = "TOTOLINK_X6000R";
const char* WIFI_PASSWORD = "DDDino1234";
const char* MQTT_BROKER   = "192.168.0.229";   // 電腦（Mosquitto broker）區網 IP
const uint16_t MQTT_PORT  = 1883;
const char* MQTT_CLIENT_ID = "NodeMCU_ECG";

// ======================= GPIO 腳位（NodeMCU-32 / ESP32） =======================
// 註：黃色按鈕（查詢上次心律）已移除，相關程式碼一併刪除。
#define LCD_SDA     21   // GPIO21 I2C SDA
#define LCD_SCL     22   // GPIO22 I2C SCL
#define BUZZER      13   // GPIO13 被動蜂鳴器（PWM）
#define LO_PLUS     34   // GPIO34 導線脫落偵測 LO+   輸入專用腳
#define LO_MINUS    35   // GPIO35 導線脫落偵測 LO-   輸入專用腳
#define BTN_BLUE    12   // GPIO12 裝置開關          INPUT_PULLUP
#define ECG_PIN     36   // GPIO36 (VP) AD8232 OUTPUT 輸入專用腳

// ======================= 被動蜂鳴器 PWM（ESP32 LEDC） =======================
#define BUZZER_CH    0      // LEDC channel
#define BUZZER_FREQ  2700   // 9042 共振頻率 (Hz)
#define BUZZER_RES   8      // 8-bit 解析度
#define BUZZER_DUTY  128    // 50% duty

// ======================= 演算法參數（對應 SDD §5） =======================
const float DC_ALPHA      = 0.995f;  // DC 基線移除
const float LEVEL_ALPHA   = 0.95f;   // 背景雜訊強度追蹤
const float NODC_OFFSET   = 8.0f;    // 動態門檻偏移（12-bit ADC，原 2.0 × 4）
const uint16_t REFRACTORY_MS   = 250;   // 不應期（防止重複觸發）
const uint16_t RR_MIN_MS       = 270;   // RR 下限（~220 BPM）
const uint16_t RR_MAX_MS       = 2000;  // RR 上限（~30 BPM）
const uint8_t  TARGET_N_BEATS  = 3;     // N 拍平均
const uint16_t SAMPLE_INTERVAL_MS = 10; // 100 Hz 採樣
const uint16_t BPM_ALERT_HIGH  = 130;   // 心律警報閾值（與 Node-RED 一致）

// 蜂鳴時長（對應 SDD §9.4）
const uint16_t BEEP_BEAT_MS   = 60;
const uint16_t BEEP_ON_MS     = 100;
const uint16_t BEEP_OFF_MS    = 300;
const uint16_t BEEP_ALERT_MS  = 100;

const uint16_t DEBOUNCE_MS    = 300;   // 按鈕軟體防彈跳（SDD §9.3）
const uint16_t LEAD_OFF_PUB_MS = 500;  // 導線脫落 MQTT 發送間隔

// ======================= 全域物件 =======================
WiFiClient   espClient;
PubSubClient mqtt(espClient);
LiquidCrystal_I2C lcd(0x27, 16, 2);   // 若無顯示請改 0x3F 或執行 I2C Scanner

// ----------- IIR 一階濾波器（移植自 MicroPython，SDD §4.2） -----------
class IIR_filter {
public:
  float alpha;
  float old_value = 0.0f;
  IIR_filter(float a) : alpha(a) {}
  float step(float value) {
    value = old_value * alpha + value * (1.0f - alpha);
    old_value = value;
    return value;
  }
  void reset() { old_value = 0.0f; }
};

IIR_filter dc_remover(DC_ALPHA);          // DC 基線移除
IIR_filter nodc_level_filter(LEVEL_ALPHA); // 背景雜訊強度追蹤

// ======================= 系統狀態機（SDD §11） =======================
enum SystemState { IDLE, MEASURING, ALERT, LEAD_OFF };
SystemState state = IDLE;
bool deviceOn = false;          // 藍色按鈕切換

// ----------- 演算法執行期變數 -----------
float n0 = 0, n1 = 0, n2 = 0;   // 三點滑動視窗
unsigned long lockout_until = 0; // 不應期結束時間
unsigned long last_peak_ms = 0;  // 上次波峰時間（算 RR）
unsigned long rr_sum = 0;        // 累積 RR 間距總和
uint8_t beat_count = 0;          // 已累積拍數
int currentBPM = 0;              // 當前心律
bool inAlert = false;

// ----------- 時序控制 -----------
unsigned long lastSample = 0;
unsigned long lastBluePress = 0;
unsigned long lastLeadOffPub = 0;

// ----------- 非阻塞蜂鳴器（SDD §9.4 / §4.2） -----------
unsigned long beep_until = 0;

void startBeep(uint16_t ms) {
  ledcWrite(BUZZER_CH, BUZZER_DUTY);   // 50% duty 產生 2700 Hz 方波
  beep_until = millis() + ms;
}

void handleBeep() {
  if (beep_until != 0 && millis() >= beep_until) {
    ledcWrite(BUZZER_CH, 0);           // 停止鳴叫
    beep_until = 0;
  }
}

// ======================= LCD 輔助（SDD §4.2） =======================
void lcdShow(const String& r1, const String& r2) {
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print(r1.substring(0, 16));
  lcd.setCursor(0, 1);
  lcd.print(r2.substring(0, 16));
}

// ======================= 演算法狀態重置（SDD §9.1） =======================
void resetAlgorithm() {
  dc_remover.reset();
  nodc_level_filter.reset();
  n0 = n1 = n2 = 0;
  lockout_until = 0;
  last_peak_ms = 0;
  rr_sum = 0;
  beat_count = 0;
  currentBPM = 0;
  inAlert = false;
}

// ======================= WiFi / MQTT =======================
void connectWiFi() {
  if (WiFi.status() == WL_CONNECTED) return;
  lcdShow("ECG Monitor", "Connecting...");
  Serial.print("[WiFi] 連線中 SSID="); Serial.println(WIFI_SSID);
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  unsigned long start = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - start < 15000) {
    delay(250);
    Serial.print(".");
    yield();
  }
  Serial.println();
  if (WiFi.status() == WL_CONNECTED) {
    Serial.print("[WiFi] 已連線! IP = "); Serial.println(WiFi.localIP());
  } else {
    Serial.println("[WiFi] 連線失敗（逾時）— 請檢查 SSID/密碼/是否為 2.4GHz");
  }
}

void connectMQTT() {
  if (mqtt.connected()) return;
  if (WiFi.status() != WL_CONNECTED) return;   // WiFi 沒通就別試 MQTT
  mqtt.setServer(MQTT_BROKER, MQTT_PORT);
  Serial.print("[MQTT] 連線中 broker="); Serial.println(MQTT_BROKER);
  unsigned long start = millis();
  while (!mqtt.connected() && millis() - start < 5000) {
    if (mqtt.connect(MQTT_CLIENT_ID)) break;
    delay(500);
    Serial.print(".");
    yield();
  }
  Serial.println();
  if (mqtt.connected()) Serial.println("[MQTT] 已連線!");
  else { Serial.print("[MQTT] 連線失敗 rc="); Serial.println(mqtt.state()); }
}

void pub(const char* topic, const String& payload) {
#ifdef OFFLINE_MODE
  // 離線測試模式：不發 MQTT，改用序列埠輸出。
  // ecg/nodc 用 Teleplot 格式畫波形，其餘印純文字方便除錯。
  if (strcmp(topic, "ecg/nodc") == 0) {
    Serial.print(">nodc:");
    Serial.println(payload);
  } else if (strcmp(topic, "ecg/bpm") == 0) {
    Serial.print(">bpm:");
    Serial.println(payload);
  } else if (strcmp(topic, "ecg/raw") != 0) {  // raw 太多筆，略過
    Serial.print("["); Serial.print(topic); Serial.print("] "); Serial.println(payload);
  }
#else
  if (mqtt.connected()) mqtt.publish(topic, payload.c_str());
#endif
}

// ======================= LCD 即時狀態顯示（SDD §8.1） =======================
void updateLCD(int rawDisp) {
  switch (state) {
    case IDLE:
      lcdShow("ECG OFF", "Press BLUE=ON");
      break;
    case LEAD_OFF:
      lcdShow("Lead Off!", "Check pads");
      break;
    case ALERT:
      lcdShow("!! ALERT !!", "BPM:" + String(currentBPM) + " HIGH!");
      break;
    case MEASURING:
      if (currentBPM > 0)
        lcdShow("ECG:" + String(rawDisp), String(currentBPM) + " BPM  OK");
      else
        lcdShow("ECG:" + String(rawDisp), "Detecting...");
      break;
  }
}

// ======================= ECG 核心處理（SDD §5） =======================
void processECG() {
  unsigned long now = millis();

  // 導線脫落偵測：LO+/LO- 任一為 HIGH 表示電極脫落
  if (digitalRead(LO_PLUS) == HIGH || digitalRead(LO_MINUS) == HIGH) {
    state = LEAD_OFF;
    if (now - lastLeadOffPub >= LEAD_OFF_PUB_MS) {
      lastLeadOffPub = now;
      pub("ecg/status", "LEAD_OFF");
    }
    return;  // 脫落時不做波峰偵測
  }

  int raw = analogRead(ECG_PIN);            // 0~4095（ESP32 12-bit ADC）

  // [步驟 1] IIR DC 移除
  float dc_val = dc_remover.step((float)raw);
  float nodc   = (float)raw - dc_val;

  // [步驟 2] 動態背景雜訊強度追蹤
  float nodc_level   = nodc_level_filter.step(fabs(nodc));
  float trigger_level = nodc_level + NODC_OFFSET;

  // 發送原始 / 去直流訊號（100 Hz）
  pub("ecg/raw",  String(raw));
  pub("ecg/nodc", String((int)nodc));

  // [步驟 3] 三點局部波峰偵測（更新滑動視窗）
  n2 = n1; n1 = n0; n0 = nodc;
  bool peak = (n1 > n2) && (n1 > n0) && (n1 > trigger_level) && (now >= lockout_until);

  if (peak) {
    lockout_until = now + REFRACTORY_MS;

    if (last_peak_ms != 0) {
      unsigned long rr = now - last_peak_ms;
      // [步驟 4] RR 間距驗證
      if (rr > RR_MIN_MS && rr < RR_MAX_MS) {
        rr_sum += rr;
        beat_count++;
        startBeep(inAlert ? BEEP_ALERT_MS : BEEP_BEAT_MS);

        // [步驟 5] N 拍平均 BPM
        if (beat_count >= TARGET_N_BEATS) {
          currentBPM = (int)(TARGET_N_BEATS * 60000.0 / rr_sum + 0.5);
          pub("ecg/bpm", String(currentBPM));

          // 警報判斷
          if (currentBPM > BPM_ALERT_HIGH && currentBPM < 220) {
            inAlert = true;
            state = ALERT;
            pub("ecg/alert", "HIGH:" + String(currentBPM));
          } else {
            inAlert = false;
            if (state == ALERT) state = MEASURING;
          }
          rr_sum = 0;
          beat_count = 0;
        }
      } else {
        // 無效 RR：清零累積，避免污染
        rr_sum = 0;
        beat_count = 0;
      }
    }
    last_peak_ms = now;
  }

  if (state == LEAD_OFF) state = MEASURING;  // 從脫落恢復
  if (state != ALERT) state = MEASURING;
}

// ======================= 按鈕處理（SDD §9） =======================
void handleButtons() {
  unsigned long now = millis();

  // 藍色按鈕：裝置開關
  if (digitalRead(BTN_BLUE) == LOW && now - lastBluePress > DEBOUNCE_MS) {
    lastBluePress = now;
    deviceOn = !deviceOn;
    if (deviceOn) {
      resetAlgorithm();
      state = MEASURING;
      startBeep(BEEP_ON_MS);
      lcdShow("ECG ON", "Measuring...");
    } else {
      state = IDLE;
      startBeep(BEEP_OFF_MS);
      lcdShow("ECG OFF", "Press BLUE=ON");
    }
  }
}

// ======================= setup / loop =======================
void setup() {
  Serial.begin(115200);

  pinMode(BTN_BLUE,   INPUT_PULLUP);
  pinMode(LO_PLUS,    INPUT);   // GPIO34 輸入專用腳，無內部上拉
  pinMode(LO_MINUS,   INPUT);   // GPIO35 輸入專用腳，無內部上拉

  // 被動蜂鳴器 PWM 初始化（LEDC）
  ledcSetup(BUZZER_CH, BUZZER_FREQ, BUZZER_RES);
  ledcAttachPin(BUZZER, BUZZER_CH);
  ledcWrite(BUZZER_CH, 0);

  // ESP32 ADC：12-bit、0~3.3V 輸入範圍
  analogSetAttenuation(ADC_11db);

  Wire.begin(LCD_SDA, LCD_SCL);
  lcd.init();
  lcd.backlight();
  lcdShow("ECG Monitor", "Press BLUE=ON");

#ifndef OFFLINE_MODE
  connectWiFi();
  connectMQTT();
#else
  Serial.println("[OFFLINE_MODE] 略過 WiFi/MQTT，資料改由序列埠輸出");
#endif

  state = IDLE;
}

void loop() {
#ifndef OFFLINE_MODE
  if (!mqtt.connected()) connectMQTT();
  mqtt.loop();
#endif

  handleButtons();
  handleBeep();

  // 量測中固定 100 Hz 取樣處理
  if (deviceOn) {
    unsigned long now = millis();
    if (now - lastSample >= SAMPLE_INTERVAL_MS) {
      lastSample = now;
      processECG();
    }
  }

  // LCD 更新（不需每個 loop 都刷，節流 200ms 避免閃爍）
  static unsigned long lastLcd = 0;
  if (millis() - lastLcd >= 200) {
    lastLcd = millis();
    updateLCD(analogRead(ECG_PIN));
  }

  yield();
}
