// admin.js — 管理員後台邏輯

const $ = (id) => document.getElementById(id);
let bpmChart = null;

async function api(path, opts = {}) {
  const res = await fetch(path, { headers: { 'Content-Type': 'application/json' }, ...opts });
  if (!res.ok) throw new Error((await res.json().catch(() => ({}))).error || res.statusText);
  return res.json();
}

// ---------- 權限守衛：非管理員導回前台 ----------
(async () => {
  try {
    const { user } = await api('/api/me');
    if (user.role !== 'admin') { location.href = 'index.html'; return; }
    $('welcome').textContent = user.username;
    $('guard').classList.add('hidden');
    $('main-view').classList.remove('hidden');
    await loadAll();
  } catch {
    $('guard-msg').textContent = '未登入，3 秒後返回登入頁…';
    setTimeout(() => (location.href = 'index.html'), 3000);
  }
})();

$('logout-btn').addEventListener('click', async () => {
  await api('/api/logout', { method: 'POST' });
  location.href = 'index.html';
});

// ---------- 新增使用者 ----------
$('add-user-form').addEventListener('submit', async (e) => {
  e.preventDefault();
  $('add-msg').textContent = '';
  try {
    await api('/api/admin/users', {
      method: 'POST',
      body: JSON.stringify({
        username: $('new-username').value,
        password: $('new-password').value,
        role: $('new-role').value,
      }),
    });
    $('add-msg').textContent = '✅ 已新增';
    $('new-username').value = ''; $('new-password').value = '';
    await loadUsers();
  } catch (err) {
    $('add-msg').textContent = '❌ ' + err.message;
  }
});

async function loadAll() {
  await Promise.all([loadUsers(), loadAlerts()]);
}

async function loadUsers() {
  const users = await api('/api/admin/users');
  $('users-tbody').innerHTML = users.map(u => `
    <tr>
      <td>${u.id}</td>
      <td>${u.username}</td>
      <td><span class="badge ${u.role}">${u.role}</span></td>
      <td>${u.bpm_count}</td>
      <td class="${u.alert_count ? 'danger' : ''}">${u.alert_count}</td>
      <td class="muted">${u.last_record ?? '—'}</td>
      <td><button class="btn-ghost view-btn" data-id="${u.id}" data-name="${u.username}">查看趨勢</button></td>
    </tr>
  `).join('');

  document.querySelectorAll('.view-btn').forEach(b =>
    b.addEventListener('click', () => loadUserChart(b.dataset.id, b.dataset.name))
  );

  // 預設載入第一位使用者
  if (users.length) loadUserChart(users[0].id, users[0].username);
}

async function loadUserChart(userId, name) {
  $('chart-title').textContent = `心律趨勢 — ${name}`;
  const rows = await api(`/api/admin/users/${userId}/bpm?limit=100`);
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
    data: { labels, datasets: [{ label: 'BPM', data, borderColor: '#457b9d',
      backgroundColor: 'rgba(69,123,157,0.1)', tension: 0.3, pointRadius: 2, fill: true }] },
    options: { responsive: true,
      scales: { y: { suggestedMin: 40, suggestedMax: 160 } },
      plugins: { legend: { display: false } } },
  });
}

async function loadAlerts() {
  const rows = await api('/api/admin/alerts');
  $('alerts-tbody').innerHTML = rows.length
    ? rows.map(r => `<tr><td>${r.created_at}</td><td>${r.username}</td><td class="danger">${r.bpm ?? '-'}</td><td>${r.message ?? ''}</td></tr>`).join('')
    : '<tr><td colspan="4" class="muted">尚無警報</td></tr>';
}
