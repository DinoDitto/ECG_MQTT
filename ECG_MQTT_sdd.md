# ECG MQTT 心電圖監測系統 — 系統設計文件 (SDD)

**文件版本：** 1.0  
**撰寫日期：** 2026-06-19  
**開發平台：** VSCode + PlatformIO  
**硬體核心：** NodeMCU (ESP8266) + AD8232 ECG Kit  

---

## 目錄

1. [系統概覽](#1-系統概覽)
2. [硬體架構](#2-硬體架構)
3. [GPIO 腳位分配](#3-gpio-腳位分配)
4. [軟體架構](#4-軟體架構)
5. [演算法設計](#5-演算法設計)
6. [MQTT 通訊協定](#6-mqtt-通訊協定)
7. [Node-RED 流程設計](#7-node-red-流程設計)
8. [LCD 顯示邏輯](#8-lcd-顯示邏輯)
9. [按鈕與蜂鳴器行為](#9-按鈕與蜂鳴器行為)
10. [開發環境設定](#10-開發環境設定)
11. [系統狀態機](#11-系統狀態機)
12. [已知限制與未來擴展](#12-已知限制與未來擴展)

---

## 1. 系統概覽

本系統為一套基於 IoT 的即時心電圖（ECG）監測平台，使用 AD8232 感測器擷取心臟電訊號，經由 NodeMCU（ESP8266）進行訊號處理與心律計算後，透過 MQTT 協定將資料傳送至伺服器端，並由 Node-RED 負責資料視覺化與異常警報。

### 1.1 系統資料流

```
[AD8232 感測器]
      │ 類比訊號 (0~1023)
      ▼
[NodeMCU ESP8266]
  ├─ IIR 低通濾波（DC 移除）
  ├─ 動態門檻波峰偵測
  ├─ N-拍平均 BPM 計算
  ├─ 導線脫落偵測
  └─ 本地輸出
       ├─ LCD 1602A（狀態顯示）
       └─ 蜂鳴器（節拍 / 警報）
      │ WiFi / MQTT
      ▼
[Mosquitto MQTT Broker]
      │
      ▼
[Node-RED]
  ├─ ECG 波形圖（Dashboard Chart）
  ├─ BPM 數值顯示
  └─ 警報流程（閾值判斷 → 通知）
```

### 1.2 主要功能

| 功能 | 說明 |
|---|---|
| 即時 ECG 波形傳輸 | 100 Hz 採樣，透過 MQTT 傳送原始訊號與去直流訊號 |
| 心律計算（BPM） | IIR 濾波 + 局部波峰偵測 + 3 拍平均 |
| 本地顯示 | 1602A LCD 顯示狀態、BPM、警報 |
| 心律警報 | BPM 超過閾值時蜂鳴器連嗶 + Node-RED 推播通知 |
| 裝置開關 | 藍色按鈕切換測量啟停 |
| 上次心律查詢 | 黃色按鈕顯示上次測量結果 3 秒 |
| 導線脫落偵測 | LO+/LO- 腳位監控，脫落立即提示 |

---

## 2. 硬體架構

### 2.1 元件清單

| 元件 | 型號 | 說明 |
|---|---|---|
| 微控制器 | NodeMCU v1.0（ESP8266） | WiFi 主控，ADC 讀取，GPIO 控制 |
| ECG 感測器 | AD8232 ECG Kit | 單導程心電圖擷取，含放大與濾波 |
| 顯示器 | LCD 1602A + I2C 模組（PCF8574） | 16×2 字元顯示，僅需 4 條線 |
| 蜂鳴器 | 主動式 3.3V / 5V | 節拍提示與警報音效 |
| 按鈕（藍） | 常開型按鈕 | 裝置開關控制 |
| 按鈕（黃） | 常開型按鈕 | 查詢上次心律 |

### 2.2 AD8232 接線

AD8232 感測器本身已內建運算放大器、帶通濾波器（0.5 Hz ~ 40 Hz）及右腿驅動電路，輸出為 0~3.3V 的放大後類比訊號。

| AD8232 腳位 | NodeMCU 腳位 | 說明 |
|---|---|---|
| 3.3V | 3.3V | 供電 |
| GND | GND | 共地 |
| OUTPUT | A0 | 類比 ECG 訊號輸入 |
| LO+ | D5 | 導線脫落偵測（正極） |
| LO- | D6 | 導線脫落偵測（負極） |

### 2.3 LCD 1602A（I2C 模式）接線

使用 I2C 模組（PCF8574 晶片背焊於 LCD 後面），大幅減少接線數量。

| I2C 模組腳位 | NodeMCU 腳位 | 說明 |
|---|---|---|
| VCC | 3.3V | 供電 |
| GND | GND | 共地 |
| SCL | D1 (GPIO5) | I2C 時脈 |
| SDA | D2 (GPIO4) | I2C 資料 |

> **I2C 位址確認：** 出廠預設多為 `0x27`，若 LCD 無顯示可嘗試 `0x3F`，或執行 I2C Scanner 程式確認。

### 2.4 按鈕接線

兩個按鈕均採用 `INPUT_PULLUP` 模式，一端接 GPIO，另一端接 GND。按下時訊號為 `LOW`。

| 按鈕 | NodeMCU 腳位 | 模式 |
|---|---|---|
| 藍色（裝置開關） | D8 (GPIO15) | INPUT_PULLUP |
| 黃色（查詢上次） | D3 (GPIO0) | INPUT_PULLUP |

### 2.5 蜂鳴器接線

使用**主動式**蜂鳴器，接上 HIGH 訊號即鳴叫，無需 PWM。

| 蜂鳴器腳位 | NodeMCU 腳位 |
|---|---|
| + 正極 | D7 (GPIO13) |
| − 負極 | GND |

> **注意：** 若使用 5V 主動式蜂鳴器且聲音不足，可加 NPN 電晶體（S8050 / 2N2222）驅動，NodeMCU D7 控制 Base，蜂鳴器由 5V 供電。

---

## 3. GPIO 腳位分配

| GPIO | NodeMCU 標示 | 連接元件 | 方向 | 說明 |
|---|---|---|---|---|
| GPIO0 | D3 | 黃色按鈕 | INPUT_PULLUP | 查詢上次心律 |
| GPIO4 | D2 | LCD SDA | I2C | I2C 資料線 |
| GPIO5 | D1 | LCD SCL | I2C | I2C 時脈線 |
| GPIO13 | D7 | 蜂鳴器 | OUTPUT | 節拍與警報音 |
| GPIO14 | D5 | LO+ | INPUT | ECG 導線脫落偵測 |
| GPIO12 | D6 | LO− | INPUT | ECG 導線脫落偵測 |
| GPIO15 | D8 | 藍色按鈕 | INPUT_PULLUP | 裝置開關 |
| ADC0 | A0 | AD8232 OUT | ANALOG IN | 心電圖類比訊號 |

---

## 4. 軟體架構

### 4.1 NodeMCU 韌體模組結構

```
ECG_MQTT_NodeMCU/
├── src/
│   └── main.cpp          ← 主程式
├── platformio.ini         ← PlatformIO 專案設定
└── lib/                  ← 相依函式庫（自動管理）
```

### 4.2 主要類別與函式

#### `IIR_filter` 類別

一階無限脈衝響應濾波器，移植自原 MicroPython 版本。

```cpp
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
};
```

**實例化：**

```cpp
IIR_filter dc_remover(0.995f);       // DC 基線移除
IIR_filter nodc_level_filter(0.95f); // 背景雜訊強度追蹤
```

#### 核心函式一覽

| 函式 | 功能 |
|---|---|
| `processECG()` | 執行完整 ECG 訊號處理流程（採樣→濾波→偵測→發送） |
| `lcdShow(r1, r2)` | 清除 LCD 並顯示兩列文字 |
| `startBeep(ms)` | 非阻塞式開始蜂鳴，記錄結束時間 |
| `handleBeep()` | 每次 loop() 呼叫，檢查是否到時間關閉蜂鳴器 |

### 4.3 PlatformIO 設定（`platformio.ini`）

```ini
[env:nodemcuv2]
platform = espressif8266
board = nodemcuv2
framework = arduino

lib_deps =
    marcoschwartz/LiquidCrystal_I2C @ ^1.1.4
    knolleary/PubSubClient @ ^2.8
```

### 4.4 相依函式庫

| 函式庫 | 版本 | 用途 |
|---|---|---|
| `ESP8266WiFi` | 內建 | WiFi 連線 |
| `PubSubClient` | ^2.8 | MQTT 客戶端 |
| `Wire` | 內建 | I2C 通訊 |
| `LiquidCrystal_I2C` | ^1.1.4 | LCD 控制 |

---

## 5. 演算法設計

本專案的訊號處理演算法移植自前期 ESP32 / MicroPython 專案（`main.py`），核心邏輯完全一致。

### 5.1 訊號處理流程

```
原始 ADC (0~1023)
      │
      ▼
[步驟 1] IIR DC 移除（α = 0.995）
      dc_val = dc_remover.step(raw)
      nodc   = raw - dc_val
      │
      ▼
[步驟 2] 動態背景雜訊強度追蹤（α = 0.95）
      nodc_level   = level_filter.step(|nodc|)
      trigger_level = nodc_level + NODC_OFFSET (2.0)
      │
      ▼
[步驟 3] 三點局部波峰偵測
      條件：n1 > n2  AND  n1 > n0  AND  n1 > trigger_level
      │                               AND  now >= lockout_until
      ▼
[步驟 4] RR 間距驗證
      條件：270 ms < RR < 2000 ms
      （對應 30 ~ 220 BPM 合理範圍）
      │
      ▼
[步驟 5] N-拍平均 BPM 計算
      累積 3 拍後：
      BPM = 3 / (tot_interval_seconds / 60)
      重置計數器
```

### 5.2 IIR 低通濾波器（DC 移除）

**目的：** ECG 訊號含有因呼吸、體動導致的低頻基線漂移（DC 偏移），必須移除才能正確偵測波峰。

**公式：**

```
output[n] = α × output[n-1] + (1 - α) × input[n]
nodc[n]   = input[n] - output[n]
```

**參數選擇：**

| 參數 | 數值 | 說明 |
|---|---|---|
| `DC_ALPHA` | 0.995 | 極接近 1，截止頻率極低（≈ 0.08 Hz @ 100 Hz 採樣），有效保留 ECG 波形 |
| `LEVEL_ALPHA` | 0.95 | 追蹤去直流訊號的絕對值平均，反映當前訊號振幅強度 |

### 5.3 動態觸發門檻

相較於固定門檻，動態門檻能自動適應不同使用者的 ECG 訊號強度，不需手動調整。

```
trigger_level = nodc_level + NODC_OFFSET
```

- `nodc_level` 是對 `|nodc|` 做 IIR 平滑，代表背景雜訊強度的動態估計值
- `NODC_OFFSET = 2.0` 確保門檻略高於背景雜訊，減少誤觸發
- 當 ECG 訊號較弱（如電極貼附不緊），門檻自動降低；訊號較強時門檻自動提高

### 5.4 三點局部波峰偵測

使用滑動視窗比較相鄰三個樣本點，判斷中間點 `n1` 是否為局部最大值：

```cpp
// 更新滑動視窗
n2 = n1; n1 = n0; n0 = nodc;

// 波峰條件
bool peak = (n1 > n2) && (n1 > n0) && (n1 > trigger_level);
```

**不應期（Refractory Period）：** 偵測到波峰後，鎖定 `250 ms` 拒絕再次觸發，防止同一個 QRS 波群的後緣或 T 波被誤判為第二個 R 波。

### 5.5 N-拍平均心率計算

累積多拍的 RR 間距後再計算 BPM，比單次 RR 間距更穩定：

```
BPM = N / (Σ RR_i / 1000 / 60)
    = N × 60000 / Σ RR_i
```

- `N = 3`（TARGET_N_BEATS），每累積 3 個有效 RR 間距計算一次 BPM
- 計算後立即清零，重新累積下 3 拍
- 無效 RR（超出 270~2000 ms 範圍）會清零累積，避免錯誤值污染結果

### 5.6 與前期 ESP32 專案對照

| 機制 | MicroPython（ESP32） | C++（NodeMCU） | 差異 |
|---|---|---|---|
| DC 移除 | `IIR_filter(0.995)` | `IIR_filter dc_remover(0.995f)` | 無 |
| 去直流訊號 | `nodc = ecg - dc_val` | `float nodc = raw - dc_val` | 無 |
| 動態門檻 | `nodc_level + NODC_OFFSET` | 相同邏輯 | 無 |
| 波峰偵測 | `n1 > n2 and n1 > n0` | `(n1 > n2) && (n1 > n0)` | 語法差異 |
| RR 範圍 | 270~2000 ms | 270~2000 ms | 無 |
| N 拍平均 | `TARGET_N_BEATS = 3` | `TARGET_N_BEATS = 3` | 無 |
| 非阻塞蜂鳴器 | `beep_until` 時間戳記 | `beep_until` + `handleBeep()` | 相同概念 |
| ADC 解析度 | 10-bit（0~1023） | 10-bit（0~1023） | 無，門檻值可直接沿用 |

---

## 6. MQTT 通訊協定

### 6.1 Broker 設定

| 項目 | 設定值 |
|---|---|
| Broker 軟體 | Mosquitto（開源） |
| 預設 Port | 1883 |
| 安裝方式 | Windows: 官網安裝包 / macOS: `brew install mosquitto` |
| Client ID | `NodeMCU_ECG` |

### 6.2 MQTT Topics

| Topic | 方向 | 資料格式 | 說明 |
|---|---|---|---|
| `ecg/raw` | NodeMCU → Broker | 整數字串 `"512"` | 原始 ADC 值（0~1023），100 Hz 發送 |
| `ecg/nodc` | NodeMCU → Broker | 整數字串 `"-15"` | 去直流後的 ECG 訊號，適合畫波形圖 |
| `ecg/bpm` | NodeMCU → Broker | 整數字串 `"72"` | 當前心律（每 3 拍更新一次） |
| `ecg/alert` | NodeMCU → Broker | 字串 `"HIGH:130"` | 心律超出閾值時發送 |
| `ecg/status` | NodeMCU → Broker | 字串 `"LEAD_OFF"` | 導線脫落時發送 |

### 6.3 發布頻率

| Topic | 發布頻率 |
|---|---|
| `ecg/raw` / `ecg/nodc` | 每 10 ms（100 Hz），裝置開啟期間持續 |
| `ecg/bpm` | 每累積 3 個有效 RR 間距更新一次（約 2~3 秒） |
| `ecg/alert` | 每次進入警報狀態時發送 |
| `ecg/status` | 導線脫落時每 500 ms 一次 |

---

## 7. Node-RED 流程設計

### 7.1 Flow 架構

```
[mqtt in: ecg/raw]  ──► [Function: 解析] ──► [Chart: ECG 波形]
                                │
[mqtt in: ecg/bpm]  ──────────► [Gauge: BPM 儀表]
                                │
[mqtt in: ecg/alert] ─► [Function: 警報判斷] ──► [Notification 節點]
                                                └► [Audio: 警報音]

[mqtt in: ecg/status] ─► [Text: 狀態顯示]
```

### 7.2 警報 Function 節點邏輯

```javascript
// Function 節點：解析 BPM 並判斷是否超出閾值
var bpm = parseInt(msg.payload);
var threshold = 100;  // 可調整

if (bpm > threshold && bpm < 220) {
    node.warn("心律過高：" + bpm + " BPM");
    msg.payload = "警報！心律 " + bpm + " BPM 超過 " + threshold;
    return [null, msg];  // 送到第二個 output（警報流程）
} else {
    return [msg, null];  // 送到第一個 output（正常顯示）
}
```

### 7.3 Node-RED 所需 Palette

```bash
# 在 Node-RED 管理介面安裝
node-red-dashboard      # Chart、Gauge、Text、Notification 節點
```

### 7.4 Dashboard 啟動

Node-RED 安裝：
```bash
npm install -g --unsafe-perm node-red
node-red
```

Dashboard 存取網址：`http://localhost:1880/ui`

---

## 8. LCD 顯示邏輯

LCD 1602A（16 欄 × 2 列）依照系統狀態顯示不同內容，所有字串限制在 16 字元以內。

### 8.1 狀態對照表

| 系統狀態 | 第一列（Row 1） | 第二列（Row 2） |
|---|---|---|
| 待機（裝置關閉） | `ECG Monitor    ` | `Press BLUE=ON  ` |
| 啟動中（WiFi 連線） | `ECG Monitor    ` | `Connecting...  ` |
| 測量中（正常） | `ECG:512        ` | `72 BPM  OK     ` |
| 測量中（偵測中） | `ECG:512        ` | `Detecting...   ` |
| 心律警報 | `!! ALERT !!    ` | `BPM:130 HIGH!  ` |
| 導線脫落 | `Lead Off!      ` | `Check pads     ` |
| 查詢上次心律 | `Last Result:   ` | `Last:72 BPM    ` |
| 裝置關閉 | `ECG OFF        ` | `Press BLUE=ON  ` |

### 8.2 I2C 位址確認程式

若 LCD 無顯示，執行以下 I2C Scanner 確認正確位址：

```cpp
#include <Wire.h>
void setup() {
  Serial.begin(9600);
  Wire.begin(D2, D1);  // SDA, SCL
  for (byte addr = 1; addr < 127; addr++) {
    Wire.beginTransmission(addr);
    if (Wire.endTransmission() == 0) {
      Serial.print("I2C device found at 0x");
      Serial.println(addr, HEX);
    }
  }
}
void loop() {}
```

---

## 9. 按鈕與蜂鳴器行為

### 9.1 藍色按鈕（裝置開關）

| 當前狀態 | 按下後 | LCD 反應 | 蜂鳴器 |
|---|---|---|---|
| 裝置關閉 | 開啟測量 | `ECG ON / Measuring...` | 短嗶 100 ms |
| 裝置開啟 | 停止測量 | `ECG OFF / Press BLUE=ON` | 長嗶 300 ms |

開啟測量時同步重置所有演算法狀態：`dc_remover`、`nodc_level_filter`、三點視窗 `n0/n1/n2`、BPM 累積器。

### 9.2 黃色按鈕（查詢上次心律）

- 按下後 LCD 顯示 `Last Result: / Last:XX BPM`，持續 3 秒
- 若從未完成測量，顯示 `No data yet`
- 3 秒後 LCD 自動恢復為當前測量狀態
- 顯示期間暫停 ECG 處理（`showLastBPM` 旗標）

### 9.3 Debounce 機制

兩個按鈕均設 300 ms 軟體防彈跳，避免機械彈跳導致重複觸發：

```cpp
if (digitalRead(BTN_BLUE) == LOW && now - lastBluePress > 300) {
    lastBluePress = now;
    // 執行動作
}
```

### 9.4 蜂鳴器行為

| 觸發情境 | 蜂鳴模式 | 時長 |
|---|---|---|
| 偵測到 R 波（正常每拍） | 短嗶一聲 | 60 ms |
| 按藍色按鈕：開啟 | 短嗶一聲 | 100 ms |
| 按藍色按鈕：關閉 | 長嗶一聲 | 300 ms |
| 心律超過閾值 | 連嗶（每拍觸發） | 100 ms × 持續 |

所有蜂鳴器控制採**非阻塞**設計，使用 `beep_until` 時間戳記，不影響 ECG 採樣與 MQTT 發送的時序精度。

---

## 10. 開發環境設定

### 10.1 必要軟體

| 軟體 | 版本 | 用途 |
|---|---|---|
| VSCode | 最新版 | 主要程式編輯器 |
| PlatformIO（VSCode 擴充） | 最新版 | NodeMCU 韌體開發與燒錄 |
| Node.js | ≥ 18.x | Node-RED 執行環境 |
| Node-RED | 最新版 | 資料視覺化與流程控制 |
| Mosquitto | 最新版 | MQTT Broker |

### 10.2 PlatformIO 安裝步驟

1. 開啟 VSCode，進入 Extensions（`Ctrl+Shift+X`）
2. 搜尋 `PlatformIO IDE` 並安裝
3. 重新啟動 VSCode
4. 點選左側 PlatformIO 圖示 → New Project
5. 板子選擇 `NodeMCU 1.0 (ESP-12E Module)`，Framework 選 `Arduino`

### 10.3 Mosquitto 設定

**Windows：**
```bat
:: 下載並安裝後，允許匿名連線（測試用）
:: 建立 mosquitto.conf：
listener 1883
allow_anonymous true

:: 啟動
mosquitto -c mosquitto.conf -v
```

**macOS：**
```bash
brew install mosquitto
brew services start mosquitto
```

### 10.4 Node-RED 安裝與啟動

```bash
npm install -g --unsafe-perm node-red
node-red
# 瀏覽器開啟 http://localhost:1880
```

安裝 Dashboard palette：`管理 palette → 搜尋 node-red-dashboard → 安裝`

### 10.5 測試 MQTT 連線

```bash
# 訂閱所有 ECG topics（終端機 1）
mosquitto_sub -h localhost -t "ecg/#" -v

# 手動發布測試訊息（終端機 2）
mosquitto_pub -h localhost -t "ecg/bpm" -m "75"
```

---

## 11. 系統狀態機

```
          開機
            │
            ▼
        ┌─────────┐
        │  IDLE   │◄──────────────────────┐
        │ 裝置關閉 │                       │
        └────┬────┘                       │
             │ 按藍色按鈕                  │
             ▼                            │
        ┌─────────┐                       │
        │MEASURING│ ─── 導線脫落 ──► ┌──────────┐
        │ 測量中  │                  │ LEAD_OFF │
        └────┬────┘◄── 重新連接 ──── └──────────┘
             │
             ├─ BPM > 閾值 ──►┌──────────┐
             │                │  ALERT   │──► 蜂鳴 + MQTT 警報
             │◄── BPM 正常 ───└──────────┘
             │
             │ 按黃色按鈕
             ▼
        ┌──────────┐
        │ SHOW_LAST│ （3 秒後自動返回）
        └──────────┘
             │
             │ 按藍色按鈕
             ▼
           IDLE
```

---

## 12. 已知限制與未來擴展

### 12.1 已知限制

| 限制 | 說明 |
|---|---|
| 單導程 ECG | AD8232 僅提供 Lead I 訊號，不足以進行完整心電圖診斷 |
| NodeMCU ADC | ESP8266 只有一個 ADC（A0），且輸入電壓範圍為 0~1V（需注意 AD8232 輸出電壓範圍） |
| MQTT QoS | 目前使用 QoS 0（最多發一次），網路不穩時可能掉包 |
| 無認證 MQTT | 測試階段使用匿名連線，正式部署應加入帳號密碼或 TLS |
| 1602A 解析度 | 僅能顯示文字狀態，無法顯示 ECG 波形圖 |
| 非醫療級裝置 | 本專案僅供學術研究，不得用於臨床診斷 |

### 12.2 未來擴展方向

| 擴展項目 | 說明 |
|---|---|
| 換用 ESP32 | ESP32 擁有 12-bit ADC、雙核心、藍牙，可提高訊號品質與新增 BLE 傳輸 |
| Pan-Tompkins 演算法 | 更完整的 QRS 偵測演算法，加入微分、平方、移動積分步驟，提高準確率 |
| FHIR 伺服器整合 | 將 BPM 資料上傳至 FHIR R4 標準醫療資料伺服器（參照前期 ESP32 專案） |
| SpO2 感測器 | 加入 MAX30102，同時測量血氧濃度（參照前期 `pulse_oximeter.py`） |
| MQTT TLS 加密 | 加入 SSL/TLS 憑證，保護傳輸資料安全 |
| OTA 更新 | 透過 ArduinoOTA 支援無線韌體更新，免除插線燒錄 |
| Node-RED LINE 通知 | 整合 LINE Notify API，警報時推播訊息至手機 |

---

*本文件依據對話設計討論內容撰寫，對應韌體版本 v1.0。*
