// RC Lap Timer – home Wi-Fi panel: status, network scan, join/forget. Part of the client, see app.js.

let lastSsid = null;   // home network shown in the Wi-Fi form
let rssi = 0;          // home Wi-Fi signal (dBm), 0 = not connected

const signal = dbm => dbm >= -60 ? 4 : dbm >= -70 ? 3 : dbm >= -80 ? 2 : 1;
const bars = dbm => `<span class="bars" title="${dbm} dBm">${[1, 2, 3, 4]
  .map(i => `<i class="${i <= signal(dbm) ? 'on' : ''}"></i>`).join('')}</span>`;

function renderWifi() {
  const ssid = `<b>${esc(S.ssid)}</b>`;
  const err = {
    'wrong password':    'wrong password',
    'network not found': 'network not found (out of range, or the name is wrong)',
    'failed':            'couldn\'t connect',
  }[S.staErr];
  const [cls, text, sum] = {
    off:        ['', 'Not on a home network. Phones join <b>RC_Lap_Timer</b>.', ''],
    connecting: ['busy', `Connecting to ${ssid}…`, 'connecting…'],
    connected:  ['ok', `Connected to ${ssid} <span id="wbars"></span><br>
                 On home Wi-Fi open <a href="http://laptimer.local">laptimer.local</a> or <a href="http://${S.staIp}">${S.staIp}</a>`,
                 '✓ ' + S.ssid],
    waiting:    ['bad', `Not connected to ${ssid}: ${err || 'not in range'}.<br>Retrying every 30 s while no phone is on RC_Lap_Timer.`,
                 err === 'wrong password' ? 'wrong password' : 'not connected'],
  }[S.sta] || ['', '', ''];
  $('wstat').className = 'wstat ' + cls;
  $('wstat').innerHTML = text;
  $('wsum').textContent = sum;
  $('wifiForget').hidden = !S.ssid;
  renderBars();
  updatePassHint();
}

function renderBars() {
  const b = $('wbars');
  if (b) b.innerHTML = rssi ? bars(rssi) : '';
}

// Same network as saved: an empty password field keeps the saved password.
function updatePassHint() {
  const same = S && S.hasPass && $('ssid').value.trim() === S.ssid;
  $('pass').placeholder = same ? 'saved' : '';
}

function renderNets(list) {
  $('nets').hidden = false;
  $('nets').innerHTML = list.length
    ? list.map(n => `<li><button type="button" data-ssid="${esc(n.ssid)}" data-free="${n.open ? 1 : 0}">
        <span>${esc(n.ssid)}</span><span>${n.open ? '' : '🔒 '}${bars(n.rssi)}</span></button></li>`).join('')
    : '<li class="hint">No networks found. The ESP32 only sees 2.4 GHz Wi-Fi.</li>';
}

// The saved network changed (also from another phone): show it unless someone is typing there.
function wifiShowSsid(ssid) {
  if (ssid !== lastSsid && document.activeElement !== $('ssid')) {
    $('ssid').value = ssid;
    lastSsid = ssid;
  }
}

function wifiRssi(dbm) {
  if (dbm !== rssi) { rssi = dbm || 0; renderBars(); }
}

function wifiInit() {
  $('wifiForm').onsubmit = async e => {
    e.preventDefault();
    const ssid = $('ssid').value.trim(), pass = $('pass').value;
    if (!ssid) { $('ssid').focus(); return; }
    const params = { ssid };
    if (pass || ssid !== S.ssid) params.pass = pass;     // otherwise the saved password stays
    if (await api('wifi', params)) { $('pass').value = ''; lastSsid = null; }
  };

  $('wifiForget').onclick = () =>
    api('wifi', { ssid: '', pass: '' }, `Forget ${S.ssid}? The timer stays on RC_Lap_Timer only.`);

  $('ssid').oninput = updatePassHint;

  $('scan').onclick = async () => {
    const btn = $('scan');
    if (!await api('wifi/scan')) {
      btn.textContent = 'Busy connecting, try again in a moment';
      setTimeout(() => { btn.textContent = 'Scan networks'; }, 3000);
      return;
    }
    btn.disabled = true;
    btn.textContent = 'Scanning…';
    let res = null;
    for (let i = 0; i < 20; i++) {                       // a scan takes ~2 s
      await new Promise(r => setTimeout(r, 700));
      try { res = await (await fetch('/api/wifi/scan', { cache: 'no-store' })).json(); } catch (e) { res = null; }
      if (res && !res.scanning) break;
    }
    btn.disabled = false;
    btn.textContent = 'Scan again';
    renderNets(res && !res.scanning ? res.networks : []);
  };

  $('nets').onclick = e => {
    const b = e.target.closest('button');
    if (!b) return;
    $('ssid').value = b.dataset.ssid;
    $('pass').value = '';
    updatePassHint();
    $('nets').hidden = true;
    if (b.dataset.free === '1') $('wifiForm').requestSubmit();
    else $('pass').focus();
  };
}
