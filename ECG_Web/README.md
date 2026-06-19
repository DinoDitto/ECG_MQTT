# ECG Web — 健康數據管理平台

訂閱 MQTT 的 ECG 資料，寫入 SQLite，提供**使用者前端**與**管理員後台**。

```
ESP32 ─MQTT→ Mosquitto ─→ server.js ─→ SQLite (ecg.db)
                              │
                              ├─ index.html  使用者前端（看自己的數據）
                              └─ admin.html  管理員後台（管所有人）
```

> 即時波形仍交給 Node-RED；本平台只儲存與管理 **BPM、警報、導線事件** 等彙整資料。

---

## 技術

- 後端：Node.js + Express
- 資料庫：SQLite（better-sqlite3）
- MQTT：mqtt.js 訂閱 `ecg/bpm`、`ecg/alert`、`ecg/status`
- 認證：express-session + bcryptjs
- 前端：原生 HTML/JS + Chart.js（CDN）

---

## 啟動步驟

### 1. 安裝依賴
```bash
cd ECG_Web
npm install
```

### 2. 設定環境變數
```bash
cp .env.example .env
# 編輯 .env：填入 MQTT_URL、SESSION_SECRET、預設管理員帳密
```

### 3. 啟動
```bash
npm start
```

啟動後：
- 使用者前端：http://localhost:3000
- 管理員後台：http://localhost:3000/admin.html

首次啟動會自動建立 `.env` 裡的預設管理員（預設 `admin / admin123`，**請務必修改**）。

---

## 前置需求

- Node.js ≥ 18（已測 v25）
- Mosquitto broker 正在執行（與 ESP32 連同一個）
- ESP32 韌體正常發送 `ecg/bpm` 等 topic

確認資料有進來（另開終端）：
```bash
mosquitto_sub -h localhost -t "ecg/#" -v
```

---

## API 一覽

| 方法 | 路徑 | 權限 | 說明 |
|---|---|---|---|
| POST | `/api/login` | 公開 | 登入 |
| POST | `/api/logout` | 登入 | 登出 |
| GET | `/api/me` | 登入 | 取得自己資訊 |
| GET | `/api/my/bpm` | 登入 | 自己的 BPM 紀錄 |
| GET | `/api/my/summary` | 登入 | 自己的統計摘要 |
| GET | `/api/my/alerts` | 登入 | 自己的警報 |
| GET | `/api/admin/users` | 管理員 | 所有使用者列表 |
| POST | `/api/admin/users` | 管理員 | 新增使用者 |
| GET | `/api/admin/users/:id/bpm` | 管理員 | 指定使用者的 BPM |
| GET | `/api/admin/alerts` | 管理員 | 全系統警報 |

---

## 多人擴展（目前為單人模式）

現在裝置資料一律歸到 `user_id = 1`（`server.js` 的 `DEVICE_USER_ID`）。
資料表已預留 `user_id`，未來多台 ESP32 時：

1. 韌體改 topic 為 `ecg/{deviceId}/bpm`
2. `server.js` 訂閱 `ecg/+/bpm`，從 topic 解析 deviceId
3. 加一張 `devices` 對照表（device → user_id），寫入時查表決定 user_id

---

## 注意事項

- 本系統為學術用途，非醫療級裝置。
- 正式部署請：改強密碼、啟用 HTTPS、MQTT 加帳密/TLS。
- `ecg.db`、`.env` 已列入 `.gitignore`，勿提交。
