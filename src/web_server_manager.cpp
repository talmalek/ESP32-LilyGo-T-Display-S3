#include "web_server_manager.h"
#include "app_manager.h"
#include "config_manager.h"
#include "dexcom_client.h"
#include <ArduinoJson.h>
#include <WiFi.h>

static const char HTML_INDEX[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html lang="en">
<head>
  <meta charset="UTF-8">
  <meta name="viewport" content="width=device-width, initial-scale=1.0">
  <title>T-Display-S3 OS Dashboard</title>
  <style>
    :root {
      --bg: #0b1120;
      --card: #1e293b;
      --card-border: #334155;
      --text: #f8fafc;
      --muted: #94a3b8;
      --accent: #38bdf8;
      --accent-hover: #0ea5e9;
      --green: #22c55e;
      --yellow: #eab308;
      --red: #ef4444;
      --purple: #a855f7;
    }
    * { box-sizing: border-box; margin: 0; padding: 0; font-family: -apple-system, BlinkMacSystemFont, "Segoe UI", Roboto, sans-serif; }
    body { background-color: var(--bg); color: var(--text); padding: 1.25rem; min-height: 100vh; display: flex; flex-direction: column; align-items: center; }
    .container { width: 100%; max-width: 640px; display: flex; flex-direction: column; gap: 1.25rem; }
    
    .header { display: flex; justify-content: space-between; align-items: center; padding-bottom: 0.5rem; border-bottom: 1px solid var(--card-border); }
    .header h1 { font-size: 1.35rem; color: var(--accent); }
    .badge { font-size: 0.8rem; padding: 0.25rem 0.6rem; border-radius: 9999px; background: #0369a1; color: #e0f2fe; font-weight: 600; }

    .card { background: var(--card); border: 1px solid var(--card-border); border-radius: 12px; padding: 1.25rem; }
    .card h2 { font-size: 1.1rem; margin-bottom: 0.9rem; color: var(--text); display: flex; align-items: center; gap: 0.5rem; justify-content: space-between; }
    
    .grid-2 { display: grid; grid-template-columns: 1fr 1fr; gap: 0.75rem; }
    .grid-3 { display: grid; grid-template-columns: 1fr 1fr 1fr; gap: 0.75rem; }
    .stat-box { background: rgba(0,0,0,0.25); border-radius: 8px; padding: 0.75rem; }
    .stat-label { font-size: 0.72rem; color: var(--muted); text-transform: uppercase; margin-bottom: 0.25rem; }
    .stat-value { font-size: 1.15rem; font-weight: 600; color: var(--text); }
    
    .cgm-box { display: flex; align-items: baseline; gap: 0.6rem; }
    .cgm-val { font-size: 2.8rem; font-weight: 700; line-height: 1; color: var(--green); }
    .cgm-unit { font-size: 1rem; color: var(--muted); }
    .cgm-trend { font-size: 1.5rem; font-weight: bold; color: var(--accent); }

    .btn-group { display: grid; grid-template-columns: repeat(auto-fit, minmax(110px, 1fr)); gap: 0.5rem; margin-top: 0.5rem; }
    button { background: #334155; color: var(--text); border: none; padding: 0.65rem 0.85rem; border-radius: 8px; font-weight: 600; cursor: pointer; transition: all 0.2s; font-size: 0.85rem; }
    button:hover { background: #475569; }
    button.primary { background: var(--accent); color: #0f172a; }
    button.primary:hover { background: var(--accent-hover); }
    button.sub-btn { background: #1e3a8a; color: #bfdbfe; font-size: 0.8rem; padding: 0.5rem; }
    button.sub-btn:hover { background: #2563eb; color: #fff; }
    button.danger { background: rgba(239, 68, 68, 0.2); color: var(--red); border: 1px solid var(--red); }
    button.danger:hover { background: var(--red); color: #fff; }

    .control-subgroup { background: rgba(0,0,0,0.2); border-radius: 8px; padding: 0.75rem; margin-top: 0.75rem; }
    .subgroup-title { font-size: 0.8rem; font-weight: 600; color: var(--muted); margin-bottom: 0.5rem; display: flex; justify-content: space-between; align-items: center; }

    .form-group { display: flex; flex-direction: column; gap: 0.35rem; margin-bottom: 0.85rem; }
    label { font-size: 0.85rem; color: var(--muted); }
    input[type="text"], input[type="password"], input[type="number"], select {
      background: #0f172a; border: 1px solid var(--card-border); color: #fff; padding: 0.65rem; border-radius: 6px; font-size: 0.95rem;
    }
    input:focus, select:focus { outline: none; border-color: var(--accent); }
    
    .slider-container { display: flex; align-items: center; gap: 1rem; margin-top: 0.4rem; }
    input[type="range"] { flex: 1; accent-color: var(--accent); }

    /* CGM Chart SVG Container */
    .chart-container { margin-top: 0.75rem; background: #0f172a; border-radius: 8px; border: 1px solid var(--card-border); padding: 0.5rem; position: relative; }
    svg.cgm-svg { width: 100%; height: 160px; display: block; overflow: visible; }

    .toast { position: fixed; bottom: 1.5rem; right: 1.5rem; background: #0284c7; color: white; padding: 0.75rem 1.25rem; border-radius: 8px; font-weight: 500; display: none; box-shadow: 0 4px 12px rgba(0,0,0,0.5); z-index: 100; }
  </style>
</head>
<body>
  <div class="container">
    <div class="header">
      <h1>LilyGO T-Display-S3</h1>
      <span class="badge" id="screenBadge">Screen: ...</span>
    </div>

    <!-- Live CGM Widget & Graph -->
    <div class="card">
      <h2>
        <span>🩸 Dexcom CGM Status</span>
        <button onclick="control('fetch_cgm')" style="padding: 0.35rem 0.75rem; font-size: 0.75rem; background:#0284c7;">🔄 Fetch Now</button>
      </h2>
      <div style="display:flex; justify-content:space-between; align-items:center; margin-bottom: 0.75rem;">
        <div class="cgm-box">
          <span class="cgm-val" id="cgmValue">--</span>
          <span class="cgm-trend" id="cgmTrend">--</span>
          <span class="cgm-unit">mg/dL</span>
        </div>
      </div>
      <div class="grid-2">
        <div class="stat-box">
          <div class="stat-label">Change (Delta)</div>
          <div class="stat-value" id="cgmDelta">--</div>
        </div>
        <div class="stat-box">
          <div class="stat-label">Reading Age</div>
          <div class="stat-value" id="cgmAge">--</div>
        </div>
      </div>

      <!-- Graph View on Web Portal -->
      <div class="chart-container">
        <div style="display:flex; justify-content:space-between; align-items:center; margin-bottom: 0.25rem; padding: 0 0.25rem;">
          <span style="font-size:0.75rem; color:var(--muted);" id="graphHeader">Historical Graph (Recent Readings)</span>
          <span style="font-size:0.75rem; color:var(--accent);" id="graphTargetLabel">Target: 70 - 180 mg/dL</span>
        </div>
        <svg class="cgm-svg" id="cgmGraphSvg" viewBox="0 0 560 160"></svg>
      </div>
    </div>

    <!-- Live Physical Screen Mirror & Capture -->
    <div class="card">
      <h2>
        <span>📷 Live Screen Mirror</span>
        <div style="display:flex; gap:0.5rem;">
          <button onclick="refreshScreenMirror()" style="padding: 0.35rem 0.75rem; font-size: 0.75rem; background:#0284c7;">🔄 Snapshot</button>
          <a id="downloadScreenshotBtn" href="/api/screenshot" download="screen.bmp" target="_blank" style="text-decoration:none;">
            <button type="button" style="padding: 0.35rem 0.75rem; font-size: 0.75rem; background:#334155;">⬇️ Download BMP</button>
          </a>
        </div>
      </h2>
      <div style="display:flex; flex-direction:column; align-items:center; justify-content:center; background:#0a0e17; border-radius:10px; border:1px solid var(--card-border); padding:1.25rem; min-height:220px;">
        <div style="position:relative; box-shadow: 0 8px 24px rgba(0,0,0,0.6); border-radius:8px; overflow:hidden; border:2px solid #38bdf8; line-height:0;">
          <img id="liveScreenImg" src="/api/screenshot" alt="Device Framebuffer" style="display:block; max-width:100%; height:auto; background:#000;" onload="onScreenLoaded()" onerror="onScreenError()" />
        </div>
        <div style="display:flex; justify-content:space-between; width:100%; max-width:320px; margin-top:0.75rem; font-size:0.75rem; color:var(--muted);">
          <span id="screenDimensions">170 x 320 px (Direct Framebuffer)</span>
          <label style="display:flex; align-items:center; gap:0.35rem; cursor:pointer;">
            <input type="checkbox" id="autoRefreshMirror" checked onchange="toggleAutoMirror(this.checked)"> Auto-refresh (2s)
          </label>
        </div>
      </div>
    </div>

    <!-- Remote Screen Control with View Switching -->
    <div class="card">
      <h2>🎮 Remote Screen Control</h2>
      
      <!-- Primary Screen Switching -->
      <div class="btn-group">
        <button onclick="control('screen_launcher')">Launcher</button>
        <button onclick="control('screen_clock')">Clock</button>
        <button onclick="control('screen_cgm')">Dexcom</button>
        <button onclick="control('screen_settings')">Settings</button>
      </div>

      <!-- CGM Screen View Controls -->
      <div class="control-subgroup">
        <div class="subgroup-title">
          <span>Dexcom CGM Views & Orientation</span>
          <span id="cgmViewBadge" style="color:var(--accent);">View: Value+Graph</span>
        </div>
        <div class="btn-group">
          <button class="sub-btn" onclick="control('cgm_set_view', 0)">Value + Graph</button>
          <button class="sub-btn" onclick="control('cgm_set_view', 1)">Value Only (Big)</button>
          <button class="sub-btn" onclick="control('cgm_set_view', 2)">Graph Only (Full)</button>
          <button class="sub-btn" onclick="control('cgm_rotate')">🔄 Rotate Screen</button>
        </div>
      </div>

      <!-- Clock Watch Face Controls -->
      <div class="control-subgroup">
        <div class="subgroup-title">
          <span>Clock Watch Faces & Orientation</span>
          <span id="clockFaceBadge" style="color:var(--accent);">Face: Modern</span>
        </div>
        <div class="btn-group">
          <button class="sub-btn" onclick="control('clock_set_face', 0)">Modern</button>
          <button class="sub-btn" onclick="control('clock_set_face', 1)">Bold Digital</button>
          <button class="sub-btn" onclick="control('clock_set_face', 2)">Classic Clean</button>
          <button class="sub-btn" onclick="control('clock_rotate')">🔄 Rotate Screen</button>
        </div>
      </div>
      
      <div style="margin-top: 1.25rem;">
        <label>Display Backlight Brightness: <b id="brightLabel">200</b></label>
        <div class="slider-container">
          <input type="range" id="brightRange" min="20" max="255" value="200" oninput="onBrightChange(this.value)">
        </div>
      </div>
    </div>

    <!-- Settings Form (Dexcom, Target Ranges, Timezone -12 to +12) -->
    <div class="card">
      <h2>⚙️ Device & Glucose Target Ranges</h2>
      <form id="settingsForm" onsubmit="saveSettings(event)">
        <div class="form-group">
          <label>Dexcom Username (Account Name)</label>
          <input type="text" id="dexUser" name="dex_user" placeholder="username or email">
        </div>
        <div class="form-group">
          <label>Dexcom Password</label>
          <input type="password" id="dexPass" name="dex_pass" placeholder="leave blank to keep unchanged">
        </div>
        
        <div class="grid-2">
          <div class="form-group">
            <label>Dexcom Server</label>
            <select id="dexServer" name="dex_server">
              <option value="NON-US">NON-US (International)</option>
              <option value="US">US (United States)</option>
            </select>
          </div>
          <div class="form-group">
            <label>Timezone (UTC -12 to +12)</label>
            <select id="tzOffset" name="tz_offset">
              <option value="-12">UTC-12 (Baker Island)</option>
              <option value="-11">UTC-11 (Samoa, Niue)</option>
              <option value="-10">UTC-10 (Hawaii)</option>
              <option value="-9">UTC-9 (Alaska)</option>
              <option value="-8">UTC-8 (Pacific Time - US/Canada)</option>
              <option value="-7">UTC-7 (Mountain Time - US/Canada)</option>
              <option value="-6">UTC-6 (Central Time - US/Canada)</option>
              <option value="-5">UTC-5 (Eastern Time - US/Canada)</option>
              <option value="-4">UTC-4 (Atlantic Time, Santiago)</option>
              <option value="-3">UTC-3 (Buenos Aires, Sao Paulo)</option>
              <option value="-2">UTC-2 (Mid-Atlantic)</option>
              <option value="-1">UTC-1 (Azores, Cape Verde)</option>
              <option value="0">UTC+0 (London, Dublin, Lisbon)</option>
              <option value="1">UTC+1 (Berlin, Paris, Rome, Madrid)</option>
              <option value="2">UTC+2 (Jerusalem, Athens, Cairo, Helsinki)</option>
              <option value="3">UTC+3 (Moscow, Riyadh, Istanbul, Nairobi)</option>
              <option value="4">UTC+4 (Dubai, Baku, Tbilisi)</option>
              <option value="5">UTC+5 (Karachi, Tashkent)</option>
              <option value="6">UTC+6 (Dhaka, Almaty)</option>
              <option value="7">UTC+7 (Bangkok, Jakarta, Hanoi)</option>
              <option value="8">UTC+8 (Singapore, Beijing, Hong Kong)</option>
              <option value="9">UTC+9 (Tokyo, Seoul)</option>
              <option value="10">UTC+10 (Sydney, Melbourne, Guam)</option>
              <option value="11">UTC+11 (Solomon Islands, Noumea)</option>
              <option value="12">UTC+12 (Auckland, Fiji)</option>
            </select>
          </div>
        </div>

        <!-- CGM Target Range Inputs -->
        <div class="grid-2">
          <div class="form-group">
            <label>CGM Target Low (mg/dL)</label>
            <input type="number" id="targetLow" name="target_low" min="40" max="150" value="70">
          </div>
          <div class="form-group">
            <label>CGM Target High (mg/dL)</label>
            <input type="number" id="targetHigh" name="target_high" min="120" max="300" value="180">
          </div>
        </div>

        <div class="form-group" style="flex-direction:row; align-items:center; gap: 0.6rem; margin-top: 0.25rem;">
          <input type="checkbox" id="time24h" name="time_24h" style="width: 1.2rem; height: 1.2rem;">
          <label for="time24h" style="cursor:pointer; color: var(--text);">Use 24-Hour Time Format</label>
        </div>
        <button type="submit" class="primary" style="width: 100%; margin-top: 0.5rem;">Save Settings</button>
      </form>
    </div>

    <!-- System Actions & Stats -->
    <div class="card">
      <h2>📊 System & Actions</h2>
      <div class="grid-2" style="margin-bottom: 1rem;">
        <div class="stat-box">
          <div class="stat-label">WiFi IP / Signal</div>
          <div class="stat-value" id="wifiStat" style="font-size: 0.95rem;">...</div>
        </div>
        <div class="stat-box">
          <div class="stat-label">Free Heap / PSRAM</div>
          <div class="stat-value" id="memStat" style="font-size: 0.95rem;">...</div>
        </div>
      </div>
      <div class="btn-group">
        <button onclick="control('launch_ap')" style="background:#0284c7;">Launch WiFi AP Portal</button>
        <button onclick="control('reboot')" class="danger">Reboot Device</button>
      </div>
    </div>
  </div>

  <div class="toast" id="toast">Settings Saved!</div>

  <script>
    let currentTargetLow = 70;
    let currentTargetHigh = 180;

    function showToast(msg) {
      const t = document.getElementById('toast');
      t.innerText = msg;
      t.style.display = 'block';
      setTimeout(() => { t.style.display = 'none'; }, 2500);
    }

    function renderCgmGraph(history, low, high) {
      const svg = document.getElementById('cgmGraphSvg');
      if (!history || history.length === 0) {
        svg.innerHTML = '<text x="280" y="85" fill="#64748b" text-anchor="middle" font-size="13">No CGM History points available yet</text>';
        return;
      }

      const W = 560;
      const H = 160;
      const padL = 36;
      const padR = 16;
      const padT = 18;
      const padB = 24;
      const chartW = W - padL - padR;
      const chartH = H - padT - padB;

      const minVal = 40;
      const maxVal = 300;
      const mapY = (v) => {
        const clamped = Math.max(minVal, Math.min(maxVal, v));
        return padT + chartH - ((clamped - minVal) / (maxVal - minVal)) * chartH;
      };

      const yLow = mapY(low);
      const yHigh = mapY(high);

      let content = `
        <defs>
          <linearGradient id="cgmAreaGrad" x1="0%" y1="0%" x2="0%" y2="100%">
            <stop offset="0%" stop-color="#22c55e" stop-opacity="0.38" />
            <stop offset="60%" stop-color="#22c55e" stop-opacity="0.15" />
            <stop offset="100%" stop-color="#22c55e" stop-opacity="0.02" />
          </linearGradient>
        </defs>
      `;

      // Grid guides & background areas
      // In-range shaded zone
      content += `<rect x="${padL}" y="${yHigh}" width="${chartW}" height="${yLow - yHigh}" fill="rgba(34, 197, 94, 0.08)" rx="4" />`;

      // Threshold lines
      content += `<line x1="${padL}" y1="${yHigh}" x2="${padL + chartW}" y2="${yHigh}" stroke="#eab308" stroke-dasharray="4,4" stroke-width="1.2" />`;
      content += `<text x="${padL - 6}" y="${yHigh + 4}" fill="#eab308" font-size="10" text-anchor="end">${high}</text>`;

      content += `<line x1="${padL}" y1="${yLow}" x2="${padL + chartW}" y2="${yLow}" stroke="#ef4444" stroke-dasharray="4,4" stroke-width="1.2" />`;
      content += `<text x="${padL - 6}" y="${yLow + 4}" fill="#ef4444" font-size="10" text-anchor="end">${low}</text>`;

      // Historical line & points
      const stepX = history.length > 1 ? chartW / (history.length - 1) : chartW / 2;
      let pathD = '';
      let pointsSvg = '';
      const baselineY = padT + chartH;

      history.forEach((val, idx) => {
        const px = padL + idx * stepX;
        const py = mapY(val);
        if (idx === 0) pathD += `M ${px} ${py}`;
        else pathD += ` L ${px} ${py}`;

        let ptCol = 'var(--green)';
        if (val < low || val > 250) ptCol = 'var(--red)';
        else if (val > high) ptCol = 'var(--yellow)';

        pointsSvg += `<circle cx="${px}" cy="${py}" r="3.5" fill="${ptCol}" stroke="#0f172a" stroke-width="1" />`;
      });

      // Translucent green gradient area fill under the curve
      if (history.length > 1) {
        const firstX = padL;
        const lastX = padL + (history.length - 1) * stepX;
        const areaD = `${pathD} L ${lastX} ${baselineY} L ${firstX} ${baselineY} Z`;
        content += `<path d="${areaD}" fill="url(#cgmAreaGrad)" />`;
      }

      content += `<path d="${pathD}" fill="none" stroke="#22c55e" stroke-width="2.5" opacity="0.9" />`;
      content += pointsSvg;

      // X-axis baseline
      content += `<line x1="${padL}" y1="${padT + chartH}" x2="${padL + chartW}" y2="${padT + chartH}" stroke="#334155" stroke-width="1" />`;
      content += `<text x="${padL}" y="${H - 6}" fill="#64748b" font-size="9">Earliest</text>`;
      content += `<text x="${padL + chartW}" y="${H - 6}" fill="#64748b" font-size="9" text-anchor="end">Latest</text>`;

      svg.innerHTML = content;
    }

    async function loadStatus() {
      try {
        const res = await fetch('/api/status');
        const data = await res.json();
        
        document.getElementById('screenBadge').innerText = 'Screen: ' + data.screen;

        // View mode badges
        const cgmModes = ['Value + Graph', 'Value Only (Big)', 'Graph Only (Full)'];
        document.getElementById('cgmViewBadge').innerText = 'View: ' + (cgmModes[data.cgm_view_mode] || 'Standard') + (data.cgm_horizontal ? ' (Landscape)' : ' (Portrait)');

        const clockFaces = ['Modern', 'Bold Digital', 'Classic Clean'];
        document.getElementById('clockFaceBadge').innerText = 'Face: ' + (clockFaces[data.clock_face_mode] || 'Modern') + (data.clock_horizontal ? ' (Landscape)' : ' (Portrait)');
        
        // Ranges
        currentTargetLow = data.config.target_low || 70;
        currentTargetHigh = data.config.target_high || 180;
        document.getElementById('graphTargetLabel').innerText = 'Target: ' + currentTargetLow + ' - ' + currentTargetHigh + ' mg/dL';

        // CGM
        const cgm = data.cgm;
        const cgmValEl = document.getElementById('cgmValue');
        cgmValEl.innerText = cgm.value > 0 ? cgm.value : '--';
        if (cgm.value < currentTargetLow || cgm.value > 250) cgmValEl.style.color = 'var(--red)';
        else if (cgm.value > currentTargetHigh) cgmValEl.style.color = 'var(--yellow)';
        else cgmValEl.style.color = 'var(--green)';

        document.getElementById('cgmTrend').innerText = cgm.trend_symbol || '';
        document.getElementById('cgmDelta').innerText = (cgm.delta >= 0 ? '+' : '') + cgm.delta + ' mg/dL';
        document.getElementById('cgmAge').innerText = cgm.age_minutes <= 1 ? 'Just now' : cgm.age_minutes + 'm ago';

        // Render Web SVG Graph
        renderCgmGraph(cgm.history || [], currentTargetLow, currentTargetHigh);

        // Settings init if form not dirty
        if (!document.getElementById('dexUser').value) {
          document.getElementById('dexUser').value = data.config.dex_user;
          document.getElementById('dexServer').value = data.config.dex_server;
          document.getElementById('tzOffset').value = String(data.config.tz_offset);
          document.getElementById('time24h').checked = data.config.time_24h;
          document.getElementById('targetLow').value = currentTargetLow;
          document.getElementById('targetHigh').value = currentTargetHigh;
          document.getElementById('brightRange').value = data.config.brightness;
          document.getElementById('brightLabel').innerText = data.config.brightness;
        }

        // Stats
        document.getElementById('wifiStat').innerText = data.wifi.ip + ' (' + data.wifi.rssi + '%)';
        document.getElementById('memStat').innerText = (data.sys.free_heap / 1024).toFixed(0) + 'K / ' + (data.sys.free_psram / (1024*1024)).toFixed(1) + 'M';
      } catch (err) {
        console.error("Status fetch error", err);
      }
    }

    let brightTimer = null;
    function onBrightChange(val) {
      document.getElementById('brightLabel').innerText = val;
      clearTimeout(brightTimer);
      brightTimer = setTimeout(() => {
        control('brightness', parseInt(val, 10));
      }, 100);
    }

    async function control(action, val = 0) {
      try {
        const res = await fetch('/api/control', {
          method: 'POST',
          headers: { 'Content-Type': 'application/json' },
          body: JSON.stringify({ action: action, value: val })
        });
        const resp = await res.json();
        showToast(resp.message || 'Action executed');
        loadStatus();
      } catch(err) {
        showToast('Control failed');
      }
    }

    async function saveSettings(e) {
      e.preventDefault();
      const payload = {
        dex_user: document.getElementById('dexUser').value,
        dex_pass: document.getElementById('dexPass').value,
        dex_server: document.getElementById('dexServer').value,
        tz_offset: parseInt(document.getElementById('tzOffset').value, 10),
        time_24h: document.getElementById('time24h').checked,
        target_low: parseInt(document.getElementById('targetLow').value, 10),
        target_high: parseInt(document.getElementById('targetHigh').value, 10)
      };

      try {
        const res = await fetch('/api/settings', {
          method: 'POST',
          headers: { 'Content-Type': 'application/json' },
          body: JSON.stringify(payload)
        });
        const resp = await res.json();
        showToast(resp.message || 'Settings saved!');
        loadStatus();
      } catch (err) {
        showToast('Failed to save settings');
      }
    }

    // Live Screen Mirror handler
    let mirrorInterval = null;
    let mirrorPending = false;

    function refreshScreenMirror() {
      if (mirrorPending) return;
      mirrorPending = true;
      const img = document.getElementById('liveScreenImg');
      const timestamp = new Date().getTime();
      img.src = '/api/screenshot?t=' + timestamp;
      const dlBtn = document.getElementById('downloadScreenshotBtn');
      if (dlBtn) dlBtn.href = '/api/screenshot?t=' + timestamp;
    }

    function onScreenLoaded() {
      mirrorPending = false;
      const img = document.getElementById('liveScreenImg');
      if (img.naturalWidth && img.naturalHeight) {
        document.getElementById('screenDimensions').innerText = img.naturalWidth + ' x ' + img.naturalHeight + ' px (Active Framebuffer)';
      }
    }

    function onScreenError() {
      mirrorPending = false;
    }

    function toggleAutoMirror(enabled) {
      if (mirrorInterval) {
        clearInterval(mirrorInterval);
        mirrorInterval = null;
      }
      if (enabled) {
        mirrorInterval = setInterval(refreshScreenMirror, 2000);
      }
    }

    loadStatus();
    setInterval(loadStatus, 3000);
    toggleAutoMirror(true);
  </script>
</body>
</html>
)rawliteral";

void WebServerManager::begin() {
    if (_running) return;
    setupRoutes();
    _server.begin();
    _running = true;
    Serial.printf("[WebServer] HTTP Server started at http://%s\n", WiFi.localIP().toString().c_str());
}

void WebServerManager::handleClient() {
    if (_running) {
        _server.handleClient();
    }
}

void WebServerManager::setupRoutes() {
    _server.enableCORS(true);
    // Use HTTP_ANY on root so GET, HEAD, and browser probes all succeed with 200
    _server.on("/", HTTP_ANY, [this]() { handleRoot(); });
    _server.on("/api/status", HTTP_ANY, [this]() { handleGetStatus(); });
    _server.on("/api/settings", HTTP_POST, [this]() { handleSaveSettings(); });
    _server.on("/api/control", HTTP_POST, [this]() { handleControl(); });
    _server.on("/api/screenshot", HTTP_GET, [this]() { handleScreenshot(); });

    // Fallback handler for preflight and unknown routes
    _server.onNotFound([this]() {
        if (_server.method() == HTTP_OPTIONS) {
            _server.sendHeader("Access-Control-Allow-Origin", "*");
            _server.sendHeader("Access-Control-Allow-Methods", "GET, POST, HEAD, OPTIONS");
            _server.sendHeader("Access-Control-Allow-Headers", "Content-Type, X-Requested-With, Origin, Accept");
            _server.sendHeader("Access-Control-Allow-Private-Network", "true");
            _server.send(204);
            return;
        }
        if (_server.uri() == "/" || _server.uri() == "/index.html") {
            handleRoot();
            return;
        }
        _server.send(404, "text/plain", "Not found");
    });
}

void WebServerManager::handleRoot() {
    _server.sendHeader("Access-Control-Allow-Origin", "*");
    _server.sendHeader("Access-Control-Allow-Private-Network", "true");
    _server.sendHeader("Cache-Control", "no-cache, no-store, must-revalidate");
    _server.sendHeader("Pragma", "no-cache");
    _server.sendHeader("Expires", "0");
    _server.send(200, "text/html; charset=utf-8", HTML_INDEX);
}

void WebServerManager::handleGetStatus() {
    _server.sendHeader("Access-Control-Allow-Origin", "*");
    _server.sendHeader("Access-Control-Allow-Private-Network", "true");
    _server.sendHeader("Cache-Control", "no-cache");
    AppConfig& cfg = ConfigManager::getInstance().getConfig();
    const CgmReading& cgm = DexcomClient::getInstance().getCachedReading();
    const std::vector<int>& history = DexcomClient::getInstance().getHistory();

    JsonDocument doc;
    doc["screen"] = AppManager::getInstance().getCurrentStateName();
    doc["cgm_view_mode"] = static_cast<int>(AppManager::getInstance().getCgmViewMode());
    doc["cgm_horizontal"] = AppManager::getInstance().isCgmHorizontal();
    doc["clock_face_mode"] = static_cast<int>(AppManager::getInstance().getClockFaceMode());
    doc["clock_horizontal"] = AppManager::getInstance().isClockHorizontal();

    JsonObject cgmObj = doc["cgm"].to<JsonObject>();
    cgmObj["value"] = cgm.value;
    cgmObj["delta"] = cgm.delta;
    cgmObj["age_minutes"] = cgm.ageMinutes;
    cgmObj["is_valid"] = cgm.isValid;

    String trendSym = "--";
    switch (cgm.trend) {
        case CgmTrend::DoubleUp: trendSym = "⇈"; break;
        case CgmTrend::SingleUp: trendSym = "↑"; break;
        case CgmTrend::FortyFiveUp: trendSym = "↗"; break;
        case CgmTrend::Flat: trendSym = "→"; break;
        case CgmTrend::FortyFiveDown: trendSym = "↘"; break;
        case CgmTrend::SingleDown: trendSym = "↓"; break;
        case CgmTrend::DoubleDown: trendSym = "⇊"; break;
        default: trendSym = "—"; break;
    }
    cgmObj["trend_symbol"] = trendSym;

    JsonArray histArr = cgmObj["history"].to<JsonArray>();
    for (int hVal : history) {
        histArr.add(hVal);
    }

    JsonObject cfgObj = doc["config"].to<JsonObject>();
    cfgObj["dex_user"] = cfg.dexcomUser;
    cfgObj["dex_server"] = cfg.dexcomServer;
    cfgObj["tz_offset"] = cfg.timezoneOffset;
    cfgObj["brightness"] = cfg.screenBrightness;
    cfgObj["time_24h"] = cfg.use24HourFormat;
    cfgObj["target_low"] = cfg.targetLow;
    cfgObj["target_high"] = cfg.targetHigh;

    JsonObject wifiObj = doc["wifi"].to<JsonObject>();
    wifiObj["ip"] = WiFi.localIP().toString();
    int rssi = WiFi.RSSI();
    int quality = constrain(2 * (rssi + 100), 0, 100);
    wifiObj["rssi"] = quality;

    JsonObject sysObj = doc["sys"].to<JsonObject>();
    sysObj["free_heap"] = ESP.getFreeHeap();
    sysObj["free_psram"] = ESP.getFreePsram();
    sysObj["uptime"] = millis() / 1000;

    String out;
    serializeJson(doc, out);
    _server.send(200, "application/json", out);
}

void WebServerManager::handleSaveSettings() {
    _server.sendHeader("Access-Control-Allow-Origin", "*");
    _server.sendHeader("Access-Control-Allow-Private-Network", "true");
    if (!_server.hasArg("plain")) {
        _server.send(400, "application/json", "{\"error\":\"Missing body\"}");
        return;
    }

    JsonDocument doc;
    DeserializationError err = deserializeJson(doc, _server.arg("plain"));
    if (err) {
        _server.send(400, "application/json", "{\"error\":\"Invalid JSON\"}");
        return;
    }

    AppConfig& cfg = ConfigManager::getInstance().getConfig();

    if (doc.containsKey("dex_user")) cfg.dexcomUser = doc["dex_user"].as<String>();
    if (doc.containsKey("dex_pass") && doc["dex_pass"].as<String>().length() > 0) {
        cfg.dexcomPass = doc["dex_pass"].as<String>();
    }
    if (doc.containsKey("dex_server")) cfg.dexcomServer = doc["dex_server"].as<String>();
    if (doc.containsKey("tz_offset")) {
        cfg.timezoneOffset = doc["tz_offset"].as<int>();
        long gmtOffset = cfg.timezoneOffset * 3600;
        configTime(gmtOffset, 0, "pool.ntp.org", "time.google.com");
    }
    if (doc.containsKey("time_24h")) cfg.use24HourFormat = doc["time_24h"].as<bool>();
    if (doc.containsKey("target_low")) cfg.targetLow = constrain(doc["target_low"].as<int>(), 40, 150);
    if (doc.containsKey("target_high")) cfg.targetHigh = constrain(doc["target_high"].as<int>(), 120, 300);

    ConfigManager::getInstance().save();

    DexcomClient::getInstance().setCredentials(cfg.dexcomUser, cfg.dexcomPass, cfg.dexcomServer);

    _server.send(200, "application/json", "{\"status\":\"ok\",\"message\":\"Settings updated successfully!\"}");
}

void WebServerManager::handleControl() {
    _server.sendHeader("Access-Control-Allow-Origin", "*");
    _server.sendHeader("Access-Control-Allow-Private-Network", "true");
    if (!_server.hasArg("plain")) {
        _server.send(400, "application/json", "{\"error\":\"Missing body\"}");
        return;
    }

    JsonDocument doc;
    deserializeJson(doc, _server.arg("plain"));
    String action = doc["action"] | "";

    if (action == "screen_launcher") {
        AppManager::getInstance().switchTo(AppState::Launcher);
    } else if (action == "screen_clock") {
        AppManager::getInstance().switchTo(AppState::Clock);
    } else if (action == "screen_cgm") {
        AppManager::getInstance().switchTo(AppState::CGM);
    } else if (action == "screen_settings") {
        AppManager::getInstance().switchTo(AppState::Settings);
    } else if (action == "cgm_toggle_view") {
        AppManager::getInstance().toggleCgmView();
    } else if (action == "cgm_set_view") {
        int v = doc["value"] | 0;
        AppManager::getInstance().setCgmView(static_cast<CgmViewMode>(v % 3));
    } else if (action == "cgm_rotate") {
        AppManager::getInstance().toggleCgmOrientation();
    } else if (action == "clock_toggle_face") {
        AppManager::getInstance().toggleClockFace();
    } else if (action == "clock_set_face") {
        int f = doc["value"] | 0;
        AppManager::getInstance().setClockFace(static_cast<ClockFaceMode>(f % 3));
    } else if (action == "clock_rotate") {
        AppManager::getInstance().toggleClockOrientation();
    } else if (action == "brightness") {
        int val = doc["value"] | 200;
        AppManager::getInstance().setBrightness(val);
    } else if (action == "fetch_cgm") {
        AppManager::getInstance().triggerImmediateCgmFetch();
    } else if (action == "launch_ap") {
        _server.send(200, "application/json", "{\"status\":\"ok\",\"message\":\"Opening WiFi Setup AP...\"}");
        delay(500);
        AppManager::getInstance().launchAPPortal();
        return;
    } else if (action == "reboot") {
        _server.send(200, "application/json", "{\"status\":\"ok\",\"message\":\"Rebooting device...\"}");
        delay(1000);
        ESP.restart();
        return;
    }

    _server.send(200, "application/json", "{\"status\":\"ok\",\"message\":\"Command executed\"}");
}

void WebServerManager::handleScreenshot() {
    TFT_eSprite& spr = AppManager::getInstance().getSprite();
    int w = spr.width();
    int h = spr.height();

    if (w <= 0 || h <= 0) {
        _server.send(500, "text/plain", "Display buffer not ready");
        return;
    }

    // BMP rows must be padded to multiples of 4 bytes
    int rowBytes = (w * 3 + 3) & ~3;
    uint32_t imageSize = (uint32_t)rowBytes * h;
    uint32_t fileSize = 54 + imageSize;

    uint8_t bmpHeader[54] = {
        'B', 'M',
        (uint8_t)(fileSize & 0xFF),
        (uint8_t)((fileSize >> 8) & 0xFF),
        (uint8_t)((fileSize >> 16) & 0xFF),
        (uint8_t)((fileSize >> 24) & 0xFF),
        0, 0, 0, 0, // reserved
        54, 0, 0, 0, // pixel offset
        40, 0, 0, 0, // DIB header size
        (uint8_t)(w & 0xFF),
        (uint8_t)((w >> 8) & 0xFF),
        (uint8_t)((w >> 16) & 0xFF),
        (uint8_t)((w >> 24) & 0xFF),
        (uint8_t)(h & 0xFF),
        (uint8_t)((h >> 8) & 0xFF),
        (uint8_t)((h >> 16) & 0xFF),
        (uint8_t)((h >> 24) & 0xFF),
        1, 0,       // color planes
        24, 0,      // bits per pixel
        0, 0, 0, 0, // BI_RGB (uncompressed)
        (uint8_t)(imageSize & 0xFF),
        (uint8_t)((imageSize >> 8) & 0xFF),
        (uint8_t)((imageSize >> 16) & 0xFF),
        (uint8_t)((imageSize >> 24) & 0xFF),
        0x13, 0x0B, 0, 0, // horiz resolution (~72 DPI)
        0x13, 0x0B, 0, 0, // vert resolution (~72 DPI)
        0, 0, 0, 0,
        0, 0, 0, 0
    };

    WiFiClient client = _server.client();
    _server.sendHeader("Access-Control-Allow-Origin", "*");
    _server.sendHeader("Access-Control-Allow-Private-Network", "true");
    _server.sendHeader("Cache-Control", "no-cache, no-store, must-revalidate");
    _server.sendHeader("Content-Disposition", "inline; filename=\"screenshot.bmp\"");
    _server.setContentLength(fileSize);
    _server.send(200, "image/bmp", "");

    // 1. Write header
    client.write(bmpHeader, 54);

    // 2. Stream BMP pixel rows (BMP stores bottom row to top row)
    uint8_t* rowBuffer = (uint8_t*)malloc(rowBytes);
    if (!rowBuffer) {
        return;
    }

    for (int y = h - 1; y >= 0; y--) {
        memset(rowBuffer, 0, rowBytes);
        int bufIdx = 0;
        for (int x = 0; x < w; x++) {
            uint16_t color = spr.readPixel(x, y);
            // RGB565 to BGR888 for BMP format
            uint8_t r = ((color >> 11) & 0x1F) * 255 / 31;
            uint8_t g = ((color >> 5) & 0x3F) * 255 / 63;
            uint8_t b = (color & 0x1F) * 255 / 31;

            rowBuffer[bufIdx++] = b;
            rowBuffer[bufIdx++] = g;
            rowBuffer[bufIdx++] = r;
        }
        client.write(rowBuffer, rowBytes);
    }

    free(rowBuffer);
}
