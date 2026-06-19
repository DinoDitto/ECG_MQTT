# ECG MQTT 心電圖即時監測系統

以 **AD8232 ECG 感測器** 擷取心電訊號，經 **ESP32** 濾波與心律計算後，透過 **MQTT** 傳送至電腦，
由 **Node-RED** 顯示即時心電圖波形並執行警示流程，同時由 **Node.js + SQLite Web 平台** 儲存與管理健康數據。

> 課程：清大醫資所 — IoT 心電圖監測專案
> 硬體：NodeMCU-32 (ESP32) + AD8232 ECG Kit + LCD1602(I2C) + 蜂鳴器 + 按鈕

---

## 1. 系統架構

```
 [AD8232 ECG 感測器]
        │ 類比訊號 (0~3.3V)
        ▼
 [ESP32 NodeMCU-32]
   ├─ IIR 濾波 + 動態門檻波峰偵測 + 3 拍平均 BPM
   ├─ LCD 顯示狀態 / BPM
   ├─ 被動蜂鳴器（每拍提示 + 警報）
   └─ 藍色按鈕（開始 / 停止測量）
        │ WiFi / MQTT
        ▼
 [Mosquitto MQTT Broker]  (電腦，1883 埠)
        │
        ├──► [Node-RED]  即時 ECG 波形 + BPM 儀表 + 警示流程   http://localhost:1880/ui
        │
        └──► [Node.js Web 平台]  歷史數據 + 管理後台 → SQLite   http://localhost:3000
```

### MQTT Topics

| Topic | 方向 | 內容 | 說明 |
|---|---|---|---|
| `ecg/raw` | ESP32 → Broker | 整數字串 | 原始 ADC 值 (0~4095) |
| `ecg/nodc` | ESP32 → Broker | 整數字串 | 去直流訊號（畫波形用） |
| `ecg/bpm` | ESP32 → Broker | 整數字串 | 心律（每 3 拍更新） |
| `ecg/alert` | ESP32 → Broker | `HIGH:135` | 心律超過閾值時發送 |
| `ecg/status` | ESP32 → Broker | `LEAD_OFF` | 導線脫落時發送 |

---

## 2. 專案結構

```
ECG_MQTT/
├── README.md               ← 本文件（總說明）
├── ECG_MQTT_sdd.md         系統設計文件 (SDD)
├── ECG_MQTT_wiring.md      硬體接線文檔
│
├── ECG_MQTT_NodeMCU/       【ESP32 韌體】PlatformIO 專案
│   ├── platformio.ini      編譯/燒錄設定（含多個測試環境）
│   └── src/
│       ├── main.cpp        ★ 正式韌體（濾波 + BPM + LCD + MQTT）
│       ├── test_blink.cpp      測試：最小燒錄驗證
│       ├── test_i2c_scanner.cpp 測試：找 LCD I2C 位址
│       ├── test_lcd.cpp        測試：LCD 顯示
│       └── test_ecg_read.cpp   測試：AD8232 原始訊號
│
├── NodeRED/
│   └── flows.json          ★ Node-RED 流程（波形圖 + 儀表 + 警示）
│
└── ECG_Web/               【Web 健康數據平台】Node.js + SQLite
    ├── server.js           MQTT 訂閱 + Express API + 靜態服務
    ├── db.js               SQLite 初始化與資料表
    ├── public/             前端（使用者 index.html / 管理員 admin.html）
    └── README.md           Web 平台專屬說明
```

---

## 3. 硬體腳位（ESP32）

| 元件 | 接腳 | GPIO | 備註 |
|---|---|---|---|
| AD8232 OUTPUT | SVP | GPIO36 | 類比輸入專用腳 |
| AD8232 LO+ | SVN | GPIO34 | 導線脫落偵測 |
| AD8232 LO- | P34 | GPIO35 | 導線脫落偵測 |
| AD8232 VCC | 3.3V | — | **接 3.3V** |
| LCD SDA | P21 | GPIO21 | I2C 資料 |
| LCD SCL | P22 | GPIO22 | I2C 時脈 |
| LCD VCC | 5V | — | **接 5V**（藍底 1602A 需 5V，I2C 位址 0x27） |
| 蜂鳴器 + | P13 | GPIO13 | 被動式，PWM 2700Hz |
| 藍色按鈕 | P12 | GPIO12 | INPUT_PULLUP，按下為 LOW |

> 詳細接線見 [ECG_MQTT_wiring.md](ECG_MQTT_wiring.md)。
> 黃色按鈕（查詢上次心律）因接線反覆短路，已從專案移除。

---

## 4. 啟動步驟（每次開機照做）

### 前置：電腦端服務

#### ① 啟動 Mosquitto MQTT Broker
Mosquitto 已安裝於 `D:\mosquitto\`，並設定為允許區網連線（`listener 1883 0.0.0.0` + `allow_anonymous true`），通常以 Windows 服務自動執行。

確認是否在跑（PowerShell）：
```powershell
Get-NetTCPConnection -LocalPort 1883 -State Listen
```
若沒在跑，重啟服務：
```powershell
Restart-Service mosquitto
```

#### ② 啟動 Web 平台
```bash
cd ECG_Web
npm install      # 第一次才需要
npm start
```
- 使用者前端：http://localhost:3000
- 管理員後台：http://localhost:3000/admin.html
- 預設管理員：`admin / admin123`（可在 `.env` 修改）

#### ③ 啟動 Node-RED
```bash
node-red
```
- 編輯器：http://localhost:1880
- Dashboard：http://localhost:1880/ui
- 首次使用需匯入 `NodeRED/flows.json`（☰ → Import → 選檔 → Deploy），
  並確認已安裝 `node-red-dashboard` palette。

### 裝置端：ESP32

#### ④ 燒錄正式韌體
用 VSCode + PlatformIO 開啟 `ECG_MQTT_NodeMCU` 資料夾：
- PlatformIO 側邊欄 → **esp32dev** → **Upload**

> WiFi / Broker IP 設定在 [main.cpp](ECG_MQTT_NodeMCU/src/main.cpp) 最上方
> （目前：SSID `TOTOLINK_X6000R`、Broker `192.168.0.229`）。
> 若換網路環境，需修改後重新燒錄。

#### ⑤ 開始測量
1. 確認 ESP32 開機連上 WiFi（Monitor 顯示 `[WiFi] 已連線!` 與 `[MQTT] 已連線!`）
2. 貼好三片電極（紅=右、黃=左、綠=參考），坐穩放鬆
3. **按藍色按鈕** → LCD 顯示 `Measuring...`
4. 在 Node-RED `/ui` 看即時波形、BPM 儀表；在 Web 平台看歷史數據

---

## 5. 演算法重點

| 機制 | 參數 | 說明 |
|---|---|---|
| IIR DC 移除 | α = 0.995 | 去除基線漂移 |
| 動態雜訊追蹤 | α = 0.95 | 自適應背景雜訊 |
| 動態觸發門檻 | nodc_level + 8.0 | 12-bit ADC（原 10-bit × 4） |
| 三點波峰偵測 | n1>n2 & n1>n0 & >門檻 | 偵測 R 波 |
| 不應期 | 250 ms | 防止重複觸發 |
| RR 有效範圍 | 270~2000 ms | 對應 30~220 BPM |
| N 拍平均 | 3 拍 | BPM = 3 × 60000 / ΣRR |
| **警報閾值** | **BPM > 130** | 超過即發 `ecg/alert` + 蜂鳴 + Node-RED 警示 |

---

## 6. 測量品質提示

ECG 訊號易受干擾，若波形雜亂、BPM 偏高：
- **務必用凝膠電極貼片**，勿用手捏電極
- 三片電極都要貼牢、皮膚擦乾淨（可沾微量水）
- 量測時**完全放鬆、不動、不說話**（肌電雜訊會造成假尖峰）
- 若仍雜訊大，改用**行動電源供電 ESP32**（避免市電交流雜訊）

---

## 7. 驗證指令（確認 MQTT 資料流）

```bash
# 訂閱所有 ECG 主題，ESP32 測量時應持續跳出資料
D:\mosquitto\mosquitto_sub.exe -h localhost -t "ecg/#" -v

# 手動發測試 BPM（驗證 Web / Node-RED 接收）
D:\mosquitto\mosquitto_pub.exe -h localhost -t "ecg/bpm" -m "75"
```

完整鏈路檢查：
1. ESP32 Monitor 印 `[MQTT] 已連線!` → 發送端 OK
2. `mosquitto_sub` 收得到 `ecg/bpm` → 資料進 Broker
3. Web server log 印 `[mqtt] 已訂閱` → 接收端 OK
4. Node-RED `/ui` 波形跳動、Web 平台出現數字 → 完整顯示

---

## 8. 已知限制

| 限制 | 說明 |
|---|---|
| 單導程 ECG | 僅 Lead I，非醫療級診斷用途 |
| 訊號易受干擾 | USB 供電與體動會增加雜訊 |
| MQTT 無加密 | 測試環境用匿名連線，正式部署應加帳密 / TLS |
| 單人模式 | 裝置資料固定歸 `DEVICE_USER_ID`（server.js），架構已預留多人 |

> **本系統僅供學術研究與教學展示，不得用於臨床診斷。**
