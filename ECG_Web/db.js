/*
 * db.js — SQLite 資料庫初始化
 * 使用 better-sqlite3（同步 API，適合本機單機部署）
 *
 * 資料表設計（已預留多人：所有資料表都帶 user_id）：
 *   users        使用者帳號（含角色 user/admin）
 *   bpm_records  心律紀錄（每 3 拍一筆）
 *   alerts       警報紀錄
 *   lead_events  導線脫落事件
 *
 * 注意：ecg/raw、ecg/nodc 為 100Hz 高頻波形，不入庫（量太大），
 *       即時波形交給 Node-RED 顯示。DB 只存有意義的彙整資料。
 */

const Database = require('better-sqlite3');
const bcrypt = require('bcryptjs');
const path = require('path');

const DB_PATH = path.join(__dirname, 'ecg.db');
const db = new Database(DB_PATH);

db.pragma('journal_mode = WAL');  // 提升並發讀寫效能

function initSchema() {
  db.exec(`
    CREATE TABLE IF NOT EXISTS users (
      id            INTEGER PRIMARY KEY AUTOINCREMENT,
      username      TEXT UNIQUE NOT NULL,
      password_hash TEXT NOT NULL,
      role          TEXT NOT NULL DEFAULT 'user',   -- 'user' | 'admin'
      created_at    TEXT NOT NULL DEFAULT (datetime('now','localtime'))
    );

    CREATE TABLE IF NOT EXISTS bpm_records (
      id          INTEGER PRIMARY KEY AUTOINCREMENT,
      user_id     INTEGER NOT NULL,
      bpm         INTEGER NOT NULL,
      recorded_at TEXT NOT NULL DEFAULT (datetime('now','localtime')),
      FOREIGN KEY (user_id) REFERENCES users(id)
    );

    CREATE TABLE IF NOT EXISTS alerts (
      id         INTEGER PRIMARY KEY AUTOINCREMENT,
      user_id    INTEGER NOT NULL,
      bpm        INTEGER,
      message    TEXT,
      created_at TEXT NOT NULL DEFAULT (datetime('now','localtime')),
      FOREIGN KEY (user_id) REFERENCES users(id)
    );

    CREATE TABLE IF NOT EXISTS lead_events (
      id         INTEGER PRIMARY KEY AUTOINCREMENT,
      user_id    INTEGER NOT NULL,
      status     TEXT NOT NULL,
      created_at TEXT NOT NULL DEFAULT (datetime('now','localtime')),
      FOREIGN KEY (user_id) REFERENCES users(id)
    );

    CREATE INDEX IF NOT EXISTS idx_bpm_user_time ON bpm_records(user_id, recorded_at);
    CREATE INDEX IF NOT EXISTS idx_alert_user_time ON alerts(user_id, created_at);
  `);
}

/**
 * 確保有預設管理員與預設使用者（單人模式，user_id=1 給裝置寫入）。
 */
function ensureDefaults(adminUser, adminPass) {
  const count = db.prepare('SELECT COUNT(*) AS c FROM users').get().c;
  if (count > 0) return;

  const hash = bcrypt.hashSync(adminPass, 10);
  // id=1 給預設管理員，裝置資料先全部歸到 id=1
  db.prepare(
    'INSERT INTO users (id, username, password_hash, role) VALUES (?,?,?,?)'
  ).run(1, adminUser, hash, 'admin');

  console.log(`[db] 已建立預設管理員：${adminUser}`);
}

module.exports = { db, initSchema, ensureDefaults, DB_PATH };

// 允許 `npm run init-db` 單獨初始化
if (require.main === module) {
  require('dotenv').config();
  initSchema();
  ensureDefaults(process.env.ADMIN_USER || 'admin', process.env.ADMIN_PASS || 'admin123');
  console.log('[db] 初始化完成：', DB_PATH);
}
