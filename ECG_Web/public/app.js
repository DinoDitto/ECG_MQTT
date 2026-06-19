// app.js — 使用者前端邏輯

const $ = (id) => document.getElementById(id);
let bpmChart = null;

async function api(path, opts = {}) {
  const res = await fetch(path, {
    headers: { 'Content-Type': 'application/json' },
    ...opts,
  });
  if (!res.ok) throw new Error((await res.json().catch(() => ({}))).error || res.statusText);
  return res.json();
}

// ---------- 登入流程 ----------
$('login-form').addEventListener('submit', async (e) => {
  e.preventDefault();
  $('login-error').textContent = '';
  try {
    await api('/api/login', {
      method: 'POST',
      body: JSON.stringify({ username: $('username').value, password: $('password').value }),
    });
    await enterApp();
  } catch (err) {
    $('login-error').textContent = err.message;
  }
});

$('logout-btn').addEventListener('click', async () => {
  await api('/api/logout', { method: 'POST' });
  location.reload();
});

$('refresh-btn').addEventListener('click', loadData);

// ---------- 進入主畫面 ----------
async function enterApp() {
  const { user } = await api('/api/me');
  $('login-view').classList.add('hidden');
  $('main-view').classList.remove('hidden');
  $('welcome').textContent = `${user.username}`;
  if (user.role === 'admin') $('admin-link').classList.remove('hidden');
  await loadData();
}

async function loadData() {
  const [summary, bpm, alerts] = await Promise.all([
    api('/api/my/summary'),
    api('/api/my/bpm?limit=100'),
    api('/api/my/alerts'),
  ]);
  renderSummary(summary);
  renderChart(bpm);
  renderAlerts(alerts);
}

function renderSummary(s) {
  $('s-last').textContent = s.count ? lastBpm() : '--';
  $('s-avg').textContent = s.avg ?? '--';
  $('s-range').textContent = s.count ? `${s.max} / ${s.min}` : '-- / --';
  $('s-alerts').textContent = s.alertCount ?? 0;
}
let _lastBpm = '--';
function lastBpm() { return _lastBpm; }

function renderChart(rows) {
  if (rows.length) _lastBpm = rows[rows.length - 1].bpm;
  $('s-last').textContent = _lastBpm;

  const labels = rows.map(r => r.recorded_at.split(' ')[1] || r.recorded_at);
  const data = rows.map(r => r.bpm);

  if (bpmChart) {
    bpmChart.data.labels = labels;
    bpmChart.data.datasets[0].data = data;
    bpmChart.update();
    return;
  }
  bpmChart = new Chart($('bpm-chart'), {
    type: 'line',
    data: {
      labels,
      datasets: [{
        label: 'BPM',
        data,
        borderColor: '#e63946',
        backgroundColor: 'rgba(230,57,70,0.1)',
        tension: 0.3,
        pointRadius: 2,
        fill: true,
      }],
    },
    options: {
      responsive: true,
      scales: { y: { suggestedMin: 40, suggestedMax: 160, title: { display: true, text: 'BPM' } } },
      plugins: { legend: { display: false } },
    },
  });
}

function renderAlerts(rows) {
  const tb = $('alert-tbody');
  if (!rows.length) { tb.innerHTML = '<tr><td colspan="3" class="muted">尚無警報紀錄</td></tr>'; return; }
  tb.innerHTML = rows.map(r =>
    `<tr><td>${r.created_at}</td><td class="danger">${r.bpm ?? '-'}</td><td>${r.message ?? ''}</td></tr>`
  ).join('');
}

// ---------- 啟動：若已登入直接進入 ----------
(async () => {
  try { await enterApp(); } catch { /* 未登入，停在登入畫面 */ }
})();
