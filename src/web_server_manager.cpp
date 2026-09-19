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
      --bg: #0f172a;
      --card: #1e293b;
      --card-border: #334155;
      --text: #f8fafc;
      --muted: #94a3b8;
      --accent: #38bdf8;
      --accent-hover: #0ea5e9;
      --green: #22c55e;
      --yellow: #eab308;
      --red: #ef4444;
    }
    * { box-sizing: border-box; margin: 0; padding: 0; font-family: -apple-system, BlinkMacSystemFont, "Segoe UI", Roboto, sans-serif; }
    body { background-color: var(--bg); color: var(--text); padding: 1.25rem; min-height: 100vh; display: flex; flex-direction: column; align-items: center; }
    .container { width: 100%; max-width: 600px; display: flex; flex-direction: column; gap: 1.25rem; }
    
    .header { display: flex; justify-content: space-between; align-items: center; padding-bottom: 0.5rem; border-bottom: 1px solid var(--card-border); }
    .header h1 { font-size: 1.35rem; color: var(--accent); }
    .badge { font-size: 0.8rem; padding: 0.25rem 0.6rem; border-radius: 9999px; background: #0369a1; color: #e0f2fe; }

    .card { background: var(--card); border: 1px solid var(--card-border); border-radius: 12px; padding: 1.25rem; }
    .card h2 { font-size: 1.1rem; margin-bottom: 0.9rem; color: var(--text); display: flex; align-items: center; gap: 0.5rem; }
    
    .grid-2 { display: grid; grid-template-columns: 1fr 1fr; gap: 0.75rem; }
    .stat-box { background: rgba(0,0,0,0.25); border-radius: 8px; padding: 0.75rem; }
    .stat-label { font-size: 0.75rem; color: var(--muted); text-transform: uppercase; margin-bottom: 0.25rem; }
    .stat-value { font-size: 1.2rem; font-weight: 600; color: var(--text); }
    
    .cgm-box { display: flex; align-items: baseline; gap: 0.6rem; }
    .cgm-val { font-size: 2.8rem; font-weight: 700; line-height: 1; color: var(--green); }
    .cgm-unit { font-size: 1rem; color: var(--muted); }
    .cgm-trend { font-size: 1.5rem; font-weight: bold; color: var(--accent); }

    .btn-group { display: grid; grid-template-columns: repeat(auto-fit, minmax(110px, 1fr)); gap: 0.5rem; margin-top: 0.5rem; }
    button { background: #334155; color: var(--text); border: none; padding: 0.65rem 1rem; border-radius: 8px; font-weight: 600; cursor: pointer; transition: all 0.2s; font-size: 0.9rem; }
    button:hover { background: #475569; }
    button.primary { background: var(--accent); color: #0f172a; }
    button.primary:hover { background: var(--accent-hover); }
    button.danger { background: rgba(239, 68, 68, 0.2); color: var(--red); border: 1px solid var(--red); }
    button.danger:hover { background: var(--red); color: #fff; }

    .form-group { display: flex; flex-direction: column; gap: 0.35rem; margin-bottom: 0.85rem; }
    label { font-size: 0.85rem; color: var(--muted); }
    input[type="text"], input[type="password"], input[type="number"], select {
      background: #0f172a; border: 1px solid var(--card-border); color: #fff; padding: 0.65rem; border-radius: 6px; font-size: 0.95rem;
    }
    input:focus, select:focus { outline: none; border-color: var(--accent); }
    
    .slider-container { display: flex; align-items: center; gap: 1rem; margin-top: 0.4rem; }
    input[type="range"] { flex: 1; accent-color: var(--accent); }

    .toast { position: fixed; bottom: 1.5rem; right: 1.5rem; background: #0284c7; color: white; padding: 0.75rem 1.25rem; border-radius: 8px; font-weight: 500; display: none; box-shadow: 0 4px 12px rgba(0,0,0,0.5); }
  </style>
</head>
<body>
  <div class="container">
    <div class="header">
      <h1>LilyGO T-Display-S3</h1>
      <span class="badge" id="screenBadge">Screen: ...</span>
    </div>

    <!-- Live CGM Widget -->
    <div class="card">
      <h2>🩸 Dexcom CGM Status</h2>
      <div style="display:flex; justify-content:space-between; align-items:center; margin-bottom: 0.75rem;">
        <div class="cgm-box">
          <span class="cgm-val" id="cgmValue">--</span>
          <span class="cgm-trend" id="cgmTrend">--</span>
          <span class="cgm-unit">mg/dL</span>
        </div>
        <button onclick="control('fetch_cgm')" style="padding: 0.4rem 0.8rem; font-size: 0.8rem;">🔄 Fetch Now</button>
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
    </div>

    <!-- Quick Device Controls -->
    <div class="card">
      <h2>🎮 Remote Screen Control</h2>
      <div class="btn-group">
        <button onclick="control('screen_launcher')">Launcher</button>
        <button onclick="control('screen_clock')">Clock</button>
        <button onclick="control('screen_cgm')">Dexcom</button>
        <button onclick="control('screen_settings')">Settings</button>
      </div>
      
      <div style="margin-top: 1.25rem;">
        <label>Display Backlight Brightness: <b id="brightLabel">200</b></label>
        <div class="slider-container">
          <input type="range" id="brightRange" min="20" max="255" value="200" oninput="onBrightChange(this.value)">
        </div>
      </div>
    </div>

    <!-- Settings Form -->
    <div class="card">
      <h2>⚙️ Device & API Settings</h2>
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
            <label>Timezone Offset (Hours)</label>
            <input type="number" id="tzOffset" name="tz_offset" min="-12" max="14" value="2">
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
          <div class="stat-value" id="wifiStat" style="font-size: 1rem;">...</div>
        </div>
        <div class="stat-box">
          <div class="stat-label">Free Heap / PSRAM</div>
          <div class="stat-value" id="memStat" style="font-size: 1rem;">...</div>
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
    function showToast(msg) {
      const t = document.getElementById('toast');
      t.innerText = msg;
      t.style.display = 'block';
      setTimeout(() => { t.style.display = 'none'; }, 2500);
    }

    async function loadStatus() {
      try {
        const res = await fetch('/api/status');
        const data = await res.json();
        
        document.getElementById('screenBadge').innerText = 'Screen: ' + data.screen;
        
        // CGM
        const cgm = data.cgm;
        const cgmValEl = document.getElementById('cgmValue');
        cgmValEl.innerText = cgm.value > 0 ? cgm.value : '--';
        if (cgm.value < 70 || cgm.value > 250) cgmValEl.style.color = 'var(--red)';
        else if (cgm.value > 180) cgmValEl.style.color = 'var(--yellow)';
        else cgmValEl.style.color = 'var(--green)';

        document.getElementById('cgmTrend').innerText = cgm.trend_symbol || '';
        document.getElementById('cgmDelta').innerText = (cgm.delta >= 0 ? '+' : '') + cgm.delta + ' mg/dL';
        document.getElementById('cgmAge').innerText = cgm.age_minutes <= 1 ? 'Just now' : cgm.age_minutes + 'm ago';

        // Settings init if form not dirty
        if (!document.getElementById('dexUser').value) {
          document.getElementById('dexUser').value = data.config.dex_user;
          document.getElementById('dexServer').value = data.config.dex_server;
          document.getElementById('tzOffset').value = data.config.tz_offset;
          document.getElementById('time24h').checked = data.config.time_24h;
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
        control('brightness', val);
      }, 150);
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
        tz_offset: parseInt(document.getElementById('tzOffset').value),
        time_24h: document.getElementById('time24h').checked
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

    loadStatus();
    setInterval(loadStatus, 3000);
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
    _server.on("/", HTTP_GET, [this]() { handleRoot(); });
    _server.on("/api/status", HTTP_GET, [this]() { handleGetStatus(); });
    _server.on("/api/settings", HTTP_POST, [this]() { handleSaveSettings(); });
    _server.on("/api/control", HTTP_POST, [this]() { handleControl(); });
}

void WebServerManager::handleRoot() {
    _server.send(200, "text/html", HTML_INDEX);
}

void WebServerManager::handleGetStatus() {
    AppConfig& cfg = ConfigManager::getInstance().getConfig();
    const CgmReading& cgm = DexcomClient::getInstance().getCachedReading();

    JsonDocument doc;
    doc["screen"] = AppManager::getInstance().getCurrentStateName();

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

    JsonObject cfgObj = doc["config"].to<JsonObject>();
    cfgObj["dex_user"] = cfg.dexcomUser;
    cfgObj["dex_server"] = cfg.dexcomServer;
    cfgObj["tz_offset"] = cfg.timezoneOffset;
    cfgObj["brightness"] = cfg.screenBrightness;
    cfgObj["time_24h"] = cfg.use24HourFormat;

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

    ConfigManager::getInstance().save();

    DexcomClient::getInstance().setCredentials(cfg.dexcomUser, cfg.dexcomPass, cfg.dexcomServer);

    _server.send(200, "application/json", "{\"status\":\"ok\",\"message\":\"Settings updated successfully!\"}");
}

void WebServerManager::handleControl() {
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
