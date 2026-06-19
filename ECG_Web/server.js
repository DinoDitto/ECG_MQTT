/*
 * server.js — ECG 健康數據平台主程式
 *
 * 三合一：
 *   1. MQTT 訂閱者：訂閱 ecg/bpm、ecg/alert、ecg/status，寫入 SQLite
 *   2. Express REST API：登入 / 查詢自己的數據 / 管理員查詢所有人
 *   3. 靜態檔案服務：使用者前端 index.html、管理員後台 admin.html
 *
 * 單人模式：裝置資料一律歸到 DEVICE_USER_ID（預設 1）。
 *           日後多人時，可改成依 MQTT topic 內的 device 解析 user_id。
 */

require('dotenv').config();
const express = require('express');
const session = require('express-session');
const mqtt = require('mqtt');
const bcrypt = require('bcryptjs');
const path = require('path');
const { db, initSchema, ensureDefaults } = require('./db');

const PORT = process.env.PORT || 3000;
const MQTT_URL = process.env.MQTT_URL || 'mqtt://localhost:1883';
const SESSION_SECRET = process.env.SESSION_SECRET || 'dev-secret';
const DEVICE_USER_ID = 3;  // 單人模式：所有裝置資料歸此使用者（3 = dino）

// ---------- 初始化 DB ----------
initSchema();
ensureDefaults(process.env.ADMIN_USER || 'admin', process.env.ADMIN_PASS || 'admin123');

// ---------- 預備 SQL 語句 ----------
const stmt = {
  insBpm:   db.prepare('INSERT INTO bpm_records (user_id, bpm) VALUES (?, ?)'),
  insAlert: db.prepare('INSERT INTO alerts (user_id, bpm, message) VALUES (?, ?, ?)'),
  insLead:  db.prepare('INSERT INTO lead_events (user_id, status) VALUES (?, ?)'),
  findUser: db.prepare('SELECT * FROM users WHERE username = ?'),
  userById: db.prepare('SELECT id, username, role, created_at FROM users WHERE id = ?'),
};

// ============================================================
//  1. MQTT 訂閱：把資料寫進 DB
// ============================================================
const client = mqtt.connect(MQTT_URL);

client.on('connect', () => {
  console.log('[mqtt] 已連線：', MQTT_URL);
  client.subscribe(['ecg/bpm', 'ecg/alert', 'ecg/status'], (err) => {
    if (err) console.error('[mqtt] 訂閱失敗', err);
    else console.log('[mqtt] 已訂閱 ecg/bpm, ecg/alert, ecg/status');
  });
});

client.on('error', (e) => console.error('[mqtt] 錯誤', e.message));

client.on('message', (topic, payload) => {
  const msg = payload.toString().trim();
  try {
    if (topic === 'ecg/bpm') {
      const bpm = parseInt(msg, 10);
      if (!isNaN(bpm)) stmt.insBpm.run(DEVICE_USER_ID, bpm);

    } else if (topic === 'ecg/alert') {
      // 格式 "HIGH:130"
      const parts = msg.split(':');
      const bpm = parseInt(parts[1], 10);
      stmt.insAlert.run(DEVICE_USER_ID, isNaN(bpm) ? null : bpm, msg);

    } else if (topic === 'ecg/status') {
      stmt.insLead.run(DEVICE_USER_ID, msg);
    }
  } catch (e) {
    console.error('[mqtt] 寫入失敗', e.message);
  }
});

// ============================================================
//  2. Express
// ============================================================
const app = express();
app.use(express.json());
app.use(session({
  secret: SESSION_SECRET,
  resave: false,
  saveUninitialized: false,
  cookie: { maxAge: 1000 * 60 * 60 * 8 },  // 8 小時
}));

// ---------- 權限中介層 ----------
function requireLogin(req, res, next) {
  if (!req.session.user) return res.status(401).json({ error: '未登入' });
  next();
}
function requireAdmin(req, res, next) {
  if (!req.session.user) return res.status(401).json({ error: '未登入' });
  if (req.session.user.role !== 'admin') return res.status(403).json({ error: '需要管理員權限' });
  next();
}

// ---------- 認證 ----------
app.post('/api/login', (req, res) => {
  const { username, password } = req.body;
  const user = stmt.findUser.get(username);
  if (!user || !bcrypt.compareSync(password, user.password_hash)) {
    return res.status(401).json({ error: '帳號或密碼錯誤' });
  }
  req.session.user = { id: user.id, username: user.username, role: user.role };
  res.json({ ok: true, user: req.session.user });
});

app.post('/api/logout', (req, res) => {
  req.session.destroy(() => res.json({ ok: true }));
});

app.get('/api/me', requireLogin, (req, res) => {
  res.json({ user: req.session.user });
});

// ---------- 使用者：查自己的數據 ----------
// 最近 BPM 紀錄
app.get('/api/my/bpm', requireLogin, (req, res) => {
  const limit = Math.min(parseInt(req.query.limit) || 100, 1000);
  const rows = db.prepare(
    'SELECT bpm, recorded_at FROM bpm_records WHERE user_id = ? ORDER BY id DESC LIMIT ?'
  ).all(req.session.user.id, limit);
  res.json(rows.reverse());  // 時間正序，方便畫圖
});

// 統計摘要
app.get('/api/my/summary', requireLogin, (req, res) => {
  const uid = req.session.user.id;
  const s = db.prepare(`
    SELECT COUNT(*) AS count, MIN(bpm) AS min, MAX(bpm) AS max,
           ROUND(AVG(bpm)) AS avg, MAX(recorded_at) AS last_time
    FROM bpm_records WHERE user_id = ?
  `).get(uid);
  const alertCount = db.prepare('SELECT COUNT(*) AS c FROM alerts WHERE user_id = ?').get(uid).c;
  res.json({ ...s, alertCount });
});

// 我的警報
app.get('/api/my/alerts', requireLogin, (req, res) => {
  const rows = db.prepare(
    'SELECT bpm, message, created_at FROM alerts WHERE user_id = ? ORDER BY id DESC LIMIT 50'
  ).all(req.session.user.id);
  res.json(rows);
});

// ---------- 管理員：查所有人 ----------
app.get('/api/admin/users', requireAdmin, (req, res) => {
  const rows = db.prepare(`
    SELECT u.id, u.username, u.role, u.created_at,
           (SELECT COUNT(*) FROM bpm_records b WHERE b.user_id = u.id) AS bpm_count,
           (SELECT COUNT(*) FROM alerts a WHERE a.user_id = u.id) AS alert_count,
           (SELECT MAX(recorded_at) FROM bpm_records b WHERE b.user_id = u.id) AS last_record
    FROM users u ORDER BY u.id
  `).all();
  res.json(rows);
});

// 新增使用者（管理員）
app.post('/api/admin/users', requireAdmin, (req, res) => {
  const { username, password, role } = req.body;
  if (!username || !password) return res.status(400).json({ error: '缺少欄位' });
  try {
    const hash = bcrypt.hashSync(password, 10);
    const info = db.prepare(
      'INSERT INTO users (username, password_hash, role) VALUES (?,?,?)'
    ).run(username, hash, role === 'admin' ? 'admin' : 'user');
    res.json({ ok: true, id: info.lastInsertRowid });
  } catch (e) {
    res.status(400).json({ error: '帳號已存在' });
  }
});

// 看特定使用者的 BPM（管理員）
app.get('/api/admin/users/:id/bpm', requireAdmin, (req, res) => {
  const limit = Math.min(parseInt(req.query.limit) || 100, 1000);
  const rows = db.prepare(
    'SELECT bpm, recorded_at FROM bpm_records WHERE user_id = ? ORDER BY id DESC LIMIT ?'
  ).all(req.params.id, limit);
  res.json(rows.reverse());
});

// 全系統警報（管理員）
app.get('/api/admin/alerts', requireAdmin, (req, res) => {
  const rows = db.prepare(`
    SELECT a.bpm, a.message, a.created_at, u.username
    FROM alerts a JOIN users u ON u.id = a.user_id
    ORDER BY a.id DESC LIMIT 100
  `).all();
  res.json(rows);
});

// ---------- 靜態前端 ----------
app.use(express.static(path.join(__dirname, 'public')));

app.listen(PORT, () => {
  console.log(`[web] http://localhost:${PORT}  （使用者前端）`);
  console.log(`[web] http://localhost:${PORT}/admin.html  （管理員後台）`);
});
