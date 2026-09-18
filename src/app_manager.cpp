#include "app_manager.h"
#include <time.h>
#include <esp_sntp.h>

// Colors
#define COLOR_BG            0x0000 // Black
#define COLOR_CARD_BG       0x18E3 // Dark Slate Grey
#define COLOR_ACCENT        0x05FF // Cyan / LilyGO Blue
#define COLOR_TEXT_MUTED    0x8410 // Mid Grey
#define COLOR_TEXT_WHITE    0xFFFF
#define COLOR_GREEN         0x07E0
#define COLOR_YELLOW        0xFFE0
#define COLOR_ORANGE        0xFD20
#define COLOR_RED           0xF800

static void btn1ClickStatic() { AppManager::getInstance().onBtn1Click(); }
static void btn2ClickStatic() { AppManager::getInstance().onBtn2Click(); }

void AppManager::begin() {
    Serial.println("[AppManager] Initializing Display...");
    initDisplay();
    Serial.println("[AppManager] Initializing Buttons...");
    initButtons();

    Serial.println("[AppManager] Loading Configurations...");
    ConfigManager::getInstance().begin();
    AppConfig& cfg = ConfigManager::getInstance().getConfig();
    Serial.printf("[Config] Dexcom User: %s | Server: %s | TZ: GMT+%d\n",
                  cfg.dexcomUser.c_str(), cfg.dexcomServer.c_str(), cfg.timezoneOffset);

    DexcomClient::getInstance().setCredentials(
        cfg.dexcomUser, cfg.dexcomPass, cfg.dexcomServer
    );

    Serial.println("[AppManager] Initializing WiFi...");
    initWiFi();

    Serial.println("[AppManager] Launching Main Menu...");
    switchTo(AppState::Launcher);
}

void AppManager::initDisplay() {
    // 1. Enable power to LCD
    pinMode(PIN_POWER_ON, OUTPUT);
    digitalWrite(PIN_POWER_ON, HIGH);
    delay(100);

    // 2. Setup Backlight pin
    pinMode(PIN_LCD_BL, OUTPUT);
    digitalWrite(PIN_LCD_BL, HIGH);

    // 3. Initialize TFT_eSPI
    _tft.begin();

    // Factory ST7789 initialization command table for LilyGO T-Display-S3
    typedef struct {
        uint8_t cmd;
        uint8_t data[14];
        uint8_t len;
    } lcd_cmd_t;

    static const lcd_cmd_t lcd_st7789v[] = {
        {0x11, {0}, 0 | 0x80},
        {0x3A, {0X05}, 1},
        {0xB2, {0X0B, 0X0B, 0X00, 0X33, 0X33}, 5},
        {0xB7, {0X75}, 1},
        {0xBB, {0X28}, 1},
        {0xC0, {0X2C}, 1},
        {0xC2, {0X01}, 1},
        {0xC3, {0X1F}, 1},
        {0xC6, {0X13}, 1},
        {0xD0, {0XA7}, 1},
        {0xD0, {0XA4, 0XA1}, 2},
        {0xD6, {0XA1}, 1},
        {0xE0, {0XF0, 0X05, 0X0A, 0X06, 0X06, 0X03, 0X2B, 0X32, 0X43, 0X36, 0X11, 0X10, 0X2B, 0X32}, 14},
        {0xE1, {0XF0, 0X08, 0X0C, 0X0B, 0X09, 0X24, 0X2B, 0X22, 0X43, 0X38, 0X15, 0X16, 0X2F, 0X37}, 14},
    };

    for (uint8_t i = 0; i < (sizeof(lcd_st7789v) / sizeof(lcd_cmd_t)); i++) {
        _tft.writecommand(lcd_st7789v[i].cmd);
        for (int j = 0; j < (lcd_st7789v[i].len & 0x7f); j++) {
            _tft.writedata(lcd_st7789v[i].data[j]);
        }
        if (lcd_st7789v[i].len & 0x80) {
            delay(120);
        }
    }

    _tft.setRotation(0); // Portrait (170x320)
    _tft.fillScreen(COLOR_BG);

    // 4. Create full screen sprite for flicker-free double buffering
    _spr.setColorDepth(16);
    _spr.createSprite(SCREEN_WIDTH, SCREEN_HEIGHT);
    Serial.println("[Display] TFT ST7789 initialized & double buffer created.");
}

void AppManager::initButtons() {
    pinMode(PIN_BUTTON_1, INPUT_PULLUP);
    pinMode(PIN_BUTTON_2, INPUT_PULLUP);
    Serial.println("[Buttons] Direct GPIO mode configured for Pin 0 (BOOT) and Pin 14 (KEY1).");
}

static void cgmBackgroundTask(void* param) {
    while (true) {
        CgmReading reading;
        std::vector<int> history;
        Serial.println("[CGM Task] Fetching reading in background...");
        DexcomClient::getInstance().fetchLatestReading(reading, history);
        Serial.printf("[CGM Task] Done. Valid: %d, Val: %d\n", reading.isValid, reading.value);
        vTaskDelay(pdMS_TO_TICKS(60000)); // Sleep 60 seconds
    }
}

void AppManager::switchTo(AppState newState) {
    _currentState = newState;
    _spr.fillSprite(COLOR_BG);
    _spr.pushSprite(0, 0);

    if (newState == AppState::Launcher) {
        drawLauncher();
    } else if (newState == AppState::Clock) {
        _lastClockDraw = 0;
        drawClock();
    } else if (newState == AppState::CGM) {
        _lastCgmDraw = 0;
        drawCGM();
        if (!_cgmTaskHandle) {
            xTaskCreatePinnedToCore(cgmBackgroundTask, "cgm_task", 8192, nullptr, 1, &_cgmTaskHandle, 0);
        }
    } else if (newState == AppState::Settings) {
        drawSettings();
    }
}

void AppManager::initWiFi() {
    WiFi.mode(WIFI_STA);

    _spr.fillSprite(COLOR_BG);
    _spr.setTextColor(COLOR_TEXT_WHITE, COLOR_BG);
    _spr.setTextDatum(MC_DATUM);
    _spr.drawString("Connecting WiFi...", SCREEN_WIDTH / 2, SCREEN_HEIGHT / 2);
    _spr.pushSprite(0, 0);

    Serial.println("[WiFi] Attempting auto-connect to stored credentials...");
    WiFi.begin();
    int attempts = 0;
    while (WiFi.status() != WL_CONNECTED && attempts < 15) {
        delay(500);
        Serial.print(".");
        attempts++;
    }
    Serial.println();

    if (WiFi.status() == WL_CONNECTED) {
        Serial.printf("[WiFi] Connected! IP: %s\n", WiFi.localIP().toString().c_str());
        AppConfig& cfg = ConfigManager::getInstance().getConfig();
        long gmtOffset = cfg.timezoneOffset * 3600;
        configTime(gmtOffset, 0, "pool.ntp.org", "time.google.com");
    } else {
        Serial.println("[WiFi] Could not connect. Launching Config Portal AP...");
        launchAPPortal();
    }
}

void AppManager::launchAPPortal() {
    Serial.println("\n>>> [AP Mode] Starting 'T-Display AP' Access Point <<<");
    _spr.fillSprite(COLOR_BG);
    _spr.setTextColor(COLOR_ACCENT, COLOR_BG);
    _spr.setTextDatum(TC_DATUM);
    _spr.drawString("WiFi Setup AP", SCREEN_WIDTH / 2, 20, 4);

    _spr.setTextColor(COLOR_TEXT_WHITE, COLOR_BG);
    _spr.drawString("Connect to Wi-Fi:", SCREEN_WIDTH / 2, 70, 2);
    _spr.setTextColor(COLOR_YELLOW, COLOR_BG);
    _spr.drawString("T-Display AP", SCREEN_WIDTH / 2, 95, 4);

    _spr.setTextColor(COLOR_TEXT_WHITE, COLOR_BG);
    _spr.drawString("Browse to:", SCREEN_WIDTH / 2, 145, 2);
    _spr.setTextColor(COLOR_GREEN, COLOR_BG);
    _spr.drawString("192.168.4.1", SCREEN_WIDTH / 2, 170, 4);

    _spr.setTextColor(COLOR_TEXT_MUTED, COLOR_BG);
    _spr.drawString("To configure WiFi", SCREEN_WIDTH / 2, 230, 2);
    _spr.drawString("& CGM Credentials", SCREEN_WIDTH / 2, 250, 2);
    _spr.pushSprite(0, 0);

    WiFiManager wm;
    wm.setConfigPortalTimeout(180); // 3 minutes timeout

    AppConfig& cfg = ConfigManager::getInstance().getConfig();

    WiFiManagerParameter custom_dex_user("dex_user", "Dexcom Username", cfg.dexcomUser.c_str(), 64);
    WiFiManagerParameter custom_dex_pass("dex_pass", "Dexcom Password", cfg.dexcomPass.c_str(), 64);
    WiFiManagerParameter custom_dex_srv("dex_server", "Dexcom Server (US/NON-US)", cfg.dexcomServer.c_str(), 10);
    char tzStr[8];
    snprintf(tzStr, sizeof(tzStr), "%d", cfg.timezoneOffset);
    WiFiManagerParameter custom_tz("tz_offset", "Timezone Offset (e.g. 2 or 3)", tzStr, 8);

    wm.addParameter(&custom_dex_user);
    wm.addParameter(&custom_dex_pass);
    wm.addParameter(&custom_dex_srv);
    wm.addParameter(&custom_tz);

    Serial.println("[WiFiManager] Captive portal active on 192.168.4.1");
    if (wm.startConfigPortal("T-Display AP")) {
        Serial.println("[WiFiManager] Configuration saved successfully!");
        cfg.dexcomUser = custom_dex_user.getValue();
        cfg.dexcomPass = custom_dex_pass.getValue();
        cfg.dexcomServer = custom_dex_srv.getValue();
        cfg.timezoneOffset = atoi(custom_tz.getValue());
        ConfigManager::getInstance().save();

        DexcomClient::getInstance().setCredentials(cfg.dexcomUser, cfg.dexcomPass, cfg.dexcomServer);

        long gmtOffset = cfg.timezoneOffset * 3600;
        configTime(gmtOffset, 0, "pool.ntp.org", "time.google.com");
    } else {
        Serial.println("[WiFiManager] Config portal timed out or skipped.");
    }

    switchTo(AppState::Launcher);
}

void AppManager::factoryReset() {
    _spr.fillSprite(COLOR_BG);
    _spr.setTextColor(COLOR_RED, COLOR_BG);
    _spr.setTextDatum(MC_DATUM);
    _spr.drawString("Factory Resetting...", SCREEN_WIDTH / 2, SCREEN_HEIGHT / 2);
    _spr.pushSprite(0, 0);
    delay(1500);

    WiFiManager wm;
    wm.resetSettings();
    ConfigManager::getInstance().resetAll();
    ESP.restart();
}

void AppManager::onBtn1Click() {
    if (_currentState == AppState::Launcher) {
        _selectedAppIndex = (_selectedAppIndex + 1) % APP_COUNT;
        drawLauncher();
    } else if (_currentState == AppState::Settings) {
        drawSettings();
    }
}

void AppManager::onBtn2Click() {
    if (_currentState == AppState::Launcher) {
        if (_selectedAppIndex == 0) switchTo(AppState::Clock);
        else if (_selectedAppIndex == 1) switchTo(AppState::CGM);
        else if (_selectedAppIndex == 2) switchTo(AppState::Settings);
    } else if (_currentState == AppState::Settings) {
        launchAPPortal();
    }
}

void AppManager::onChordPress() {
    if (_currentState != AppState::Launcher) {
        switchTo(AppState::Launcher);
    }
}

void AppManager::update() {
    bool b1Current = (digitalRead(PIN_BUTTON_1) == LOW); // active low
    bool b2Current = (digitalRead(PIN_BUTTON_2) == LOW); // active low
    unsigned long now = millis();

    // 1. Dual-press (Chord) detection: if both buttons held down for > 300ms
    if (b1Current && b2Current) {
        if (!_chordHandled) {
            _chordHandled = true;
            _b1Handled = true;
            _b2Handled = true;
            Serial.println("[Buttons] Chord (B1+B2) detected! Returning to Launcher.");
            onChordPress();
            return;
        }
    } else {
        if (!b1Current && !b2Current) {
            _chordHandled = false;
        }
    }

    // 2. Button 1 (BOOT) Debounce & Click Detection
    if (b1Current && !_b1Prev) {
        _b1DownTime = now;
        _b1Handled = false;
    } else if (!b1Current && _b1Prev) {
        if (!_b1Handled && (now - _b1DownTime >= 40) && (now - _b1DownTime < 1000)) {
            _b1Handled = true;
            Serial.println("[Buttons] Button 1 CLICK!");
            onBtn1Click();
        }
    }
    _b1Prev = b1Current;

    // 3. Button 2 (IO14) Debounce & Click Detection
    if (b2Current && !_b2Prev) {
        _b2DownTime = now;
        _b2Handled = false;
    } else if (!b2Current && _b2Prev) {
        if (!_b2Handled && (now - _b2DownTime >= 40) && (now - _b2DownTime < 1000)) {
            _b2Handled = true;
            Serial.println("[Buttons] Button 2 CLICK!");
            onBtn2Click();
        }
    }
    _b2Prev = b2Current;

    // App periodic UI updates
    if (_currentState == AppState::Clock) {
        updateTime();
    } else if (_currentState == AppState::CGM) {
        updateCGM();
    }
}

void AppManager::drawLauncher() {
    _spr.fillSprite(COLOR_BG);

    // Header
    _spr.setTextColor(COLOR_ACCENT, COLOR_BG);
    _spr.setTextDatum(TC_DATUM);
    _spr.drawString("T-DISPLAY S3", SCREEN_WIDTH / 2, 16, 2);

    _spr.setTextColor(COLOR_TEXT_MUTED, COLOR_BG);
    _spr.drawString("B1: Next | B2: Select", SCREEN_WIDTH / 2, 36, 1);

    // App Cards
    const char* appNames[APP_COUNT] = {"Clock", "Dexcom CGM", "Settings"};
    const char* appDescs[APP_COUNT] = {"Digital NTP Clock", "Live Blood Glucose", "WiFi & Preferences"};

    int cardWidth = 150;
    int cardHeight = 65;
    int startY = 60;
    int spacing = 18;

    for (int i = 0; i < APP_COUNT; i++) {
        int y = startY + i * (cardHeight + spacing);
        bool isSelected = (i == _selectedAppIndex);

        uint16_t borderCol = isSelected ? COLOR_ACCENT : COLOR_CARD_BG;
        uint16_t bgCol = isSelected ? 0x0A24 : COLOR_CARD_BG;

        _spr.fillRoundRect(10, y, cardWidth, cardHeight, 8, bgCol);
        _spr.drawRoundRect(10, y, cardWidth, cardHeight, 8, borderCol);
        if (isSelected) {
            _spr.drawRoundRect(9, y - 1, cardWidth + 2, cardHeight + 2, 8, borderCol);
        }

        _spr.setTextDatum(TL_DATUM);
        _spr.setTextColor(isSelected ? COLOR_TEXT_WHITE : 0xC618, bgCol);
        _spr.drawString(appNames[i], 20, y + 14, 4);

        _spr.setTextColor(isSelected ? COLOR_ACCENT : COLOR_TEXT_MUTED, bgCol);
        _spr.drawString(appDescs[i], 20, y + 42, 1);
    }

    _spr.pushSprite(0, 0);
}

void AppManager::updateTime() {
    if (millis() - _lastClockDraw >= 500) {
        _lastClockDraw = millis();
        drawClock();
    }
}

void AppManager::drawClock() {
    _spr.fillSprite(COLOR_BG);

    time_t now = time(nullptr);
    struct tm timeinfo;
    localtime_r(&now, &timeinfo);

    // Top status bar
    _spr.setTextColor(COLOR_TEXT_MUTED, COLOR_BG);
    _spr.setTextDatum(TL_DATUM);
    _spr.drawString((WiFi.status() == WL_CONNECTED) ? "WiFi: OK" : "WiFi: DIS", 8, 8, 1);

    _spr.setTextDatum(TR_DATUM);
    _spr.drawString("B1+B2: Exit", SCREEN_WIDTH - 8, 8, 1);

    // Day & Date
    char dateBuf[32];
    strftime(dateBuf, sizeof(dateBuf), "%A", &timeinfo);
    _spr.setTextDatum(TC_DATUM);
    _spr.setTextColor(COLOR_ACCENT, COLOR_BG);
    _spr.drawString(dateBuf, SCREEN_WIDTH / 2, 45, 4);

    strftime(dateBuf, sizeof(dateBuf), "%b %d, %Y", &timeinfo);
    _spr.setTextColor(COLOR_TEXT_MUTED, COLOR_BG);
    _spr.drawString(dateBuf, SCREEN_WIDTH / 2, 80, 2);

    // Large Time Display
    char timeBuf[16];
    AppConfig& cfg = ConfigManager::getInstance().getConfig();
    if (cfg.use24HourFormat) {
        strftime(timeBuf, sizeof(timeBuf), "%H:%M", &timeinfo);
    } else {
        strftime(timeBuf, sizeof(timeBuf), "%I:%M", &timeinfo);
    }

    _spr.setTextColor(COLOR_TEXT_WHITE, COLOR_BG);
    _spr.drawString(timeBuf, SCREEN_WIDTH / 2, 125, 7); // Font 7 = 7-segment big font

    // Seconds
    char secBuf[8];
    strftime(secBuf, sizeof(secBuf), ":%S", &timeinfo);
    _spr.setTextColor(COLOR_ACCENT, COLOR_BG);
    _spr.drawString(secBuf, SCREEN_WIDTH / 2, 185, 4);

    // Battery / Footer
    _spr.drawFastHLine(15, 275, SCREEN_WIDTH - 30, COLOR_CARD_BG);
    _spr.setTextColor(COLOR_TEXT_MUTED, COLOR_BG);
    _spr.setTextDatum(BC_DATUM);
    _spr.drawString("T-Display-S3 Clock", SCREEN_WIDTH / 2, 310, 2);

    _spr.pushSprite(0, 0);
}

void AppManager::updateCGM() {
    unsigned long now = millis();
    if (now - _lastCgmDraw >= 1000) {
        _lastCgmDraw = now;
        drawCGM();
    }
}

void AppManager::drawCGM() {
    _spr.fillSprite(COLOR_BG);

    const CgmReading& cgm = DexcomClient::getInstance().getCachedReading();
    const std::vector<int>& history = DexcomClient::getInstance().getHistory();

    // Top status bar
    _spr.setTextColor(COLOR_TEXT_MUTED, COLOR_BG);
    _spr.setTextDatum(TL_DATUM);
    _spr.drawString("DEXCOM CGM", 8, 8, 2);

    _spr.setTextDatum(TR_DATUM);
    _spr.drawString("B1+B2: Exit", SCREEN_WIDTH - 8, 8, 1);

    if (!cgm.isValid) {
        _spr.setTextColor(COLOR_YELLOW, COLOR_BG);
        _spr.setTextDatum(MC_DATUM);
        _spr.drawString(cgm.statusMessage, SCREEN_WIDTH / 2, 140, 2);
        _spr.setTextColor(COLOR_TEXT_MUTED, COLOR_BG);
        _spr.drawString("Fetching data...", SCREEN_WIDTH / 2, 170, 2);
        _spr.pushSprite(0, 0);
        return;
    }

    // Determine color based on glucose range
    uint16_t valColor = COLOR_GREEN;
    if (cgm.value < 70) valColor = COLOR_RED;
    else if (cgm.value > 250) valColor = COLOR_RED;
    else if (cgm.value > 180) valColor = COLOR_YELLOW;

    // Big Glucose Number
    _spr.setTextColor(valColor, COLOR_BG);
    _spr.setTextDatum(TC_DATUM);
    _spr.drawString(String(cgm.value), SCREEN_WIDTH / 2, 40, 7);

    // mg/dL label & Delta & Trend Arrow
    String trendSym = "->";
    switch (cgm.trend) {
        case CgmTrend::DoubleUp: trendSym = "^^"; break;
        case CgmTrend::SingleUp: trendSym = "^"; break;
        case CgmTrend::FortyFiveUp: trendSym = "/^"; break;
        case CgmTrend::Flat: trendSym = "->"; break;
        case CgmTrend::FortyFiveDown: trendSym = "\\v"; break;
        case CgmTrend::SingleDown: trendSym = "v"; break;
        case CgmTrend::DoubleDown: trendSym = "vv"; break;
        default: trendSym = "--"; break;
    }

    String deltaStr = (cgm.delta >= 0) ? ("+" + String(cgm.delta)) : String(cgm.delta);
    String subInfo = "mg/dL  " + deltaStr + "  " + trendSym;

    _spr.setTextColor(COLOR_TEXT_WHITE, COLOR_BG);
    _spr.drawString(subInfo, SCREEN_WIDTH / 2, 105, 4);

    // Reading age
    String ageStr = (cgm.ageMinutes <= 1) ? "Just now" : (String(cgm.ageMinutes) + " mins ago");
    _spr.setTextColor(COLOR_TEXT_MUTED, COLOR_BG);
    _spr.drawString(ageStr, SCREEN_WIDTH / 2, 135, 2);

    // Mini Trend Graph (last 2 hours)
    int graphX = 15;
    int graphY = 165;
    int graphW = SCREEN_WIDTH - 30;
    int graphH = 110;

    _spr.fillRoundRect(graphX, graphY, graphW, graphH, 6, COLOR_CARD_BG);
    _spr.drawRoundRect(graphX, graphY, graphW, graphH, 6, 0x39E7);

    // Target Range threshold lines (70 and 180 mg/dL mapped to graphH)
    // Scale: 40 mg/dL (bottom) to 300 mg/dL (top)
    int minG = 40;
    int maxG = 300;
    auto mapY = [&](int val) {
        int clamped = constrain(val, minG, maxG);
        return graphY + graphH - (int)((clamped - minG) * (float)graphH / (maxG - minG));
    };

    int line70 = mapY(70);
    int line180 = mapY(180);
    _spr.drawFastHLine(graphX + 2, line70, graphW - 4, 0x8000);   // Dark Red target line
    _spr.drawFastHLine(graphX + 2, line180, graphW - 4, 0x8400);  // Dark Yellow target line

    // Plot history points
    if (history.size() > 1) {
        int pointCount = history.size();
        float stepX = (float)(graphW - 16) / (float)(pointCount - 1);

        for (size_t i = 0; i < history.size(); i++) {
            int px = graphX + 8 + (int)(i * stepX);
            int py = mapY(history[i]);

            uint16_t ptColor = COLOR_GREEN;
            if (history[i] < 70 || history[i] > 250) ptColor = COLOR_RED;
            else if (history[i] > 180) ptColor = COLOR_YELLOW;

            _spr.fillCircle(px, py, 2, ptColor);

            if (i > 0) {
                int prevPx = graphX + 8 + (int)((i - 1) * stepX);
                int prevPy = mapY(history[i - 1]);
                _spr.drawLine(prevPx, prevPy, px, py, COLOR_TEXT_MUTED);
            }
        }
    }

    _spr.setTextColor(COLOR_TEXT_MUTED, COLOR_BG);
    _spr.setTextDatum(BC_DATUM);
    _spr.drawString("Dexcom Continuous Monitor", SCREEN_WIDTH / 2, 310, 1);

    _spr.pushSprite(0, 0);
}

void AppManager::drawSettings() {
    _spr.fillSprite(COLOR_BG);

    _spr.setTextColor(COLOR_ACCENT, COLOR_BG);
    _spr.setTextDatum(TC_DATUM);
    _spr.drawString("SETTINGS", SCREEN_WIDTH / 2, 16, 4);

    _spr.setTextColor(COLOR_TEXT_MUTED, COLOR_BG);
    _spr.drawString("B1+B2: Exit to Launcher", SCREEN_WIDTH / 2, 46, 1);

    AppConfig& cfg = ConfigManager::getInstance().getConfig();

    // Option 1: Configure WiFi & Dexcom AP Portal
    _spr.fillRoundRect(10, 75, 150, 70, 8, COLOR_CARD_BG);
    _spr.drawRoundRect(10, 75, 150, 70, 8, COLOR_ACCENT);
    _spr.setTextColor(COLOR_TEXT_WHITE, COLOR_CARD_BG);
    _spr.setTextDatum(TL_DATUM);
    _spr.drawString("Setup Portal (AP)", 20, 88, 2);
    _spr.setTextColor(COLOR_YELLOW, COLOR_CARD_BG);
    _spr.drawString("Press B2 to open AP", 20, 115, 1);

    // Info Box
    _spr.fillRoundRect(10, 160, 150, 120, 8, COLOR_CARD_BG);
    _spr.setTextColor(COLOR_TEXT_WHITE, COLOR_CARD_BG);
    _spr.drawString("Configuration:", 20, 172, 2);

    _spr.setTextColor(COLOR_TEXT_MUTED, COLOR_CARD_BG);
    _spr.drawString("Server: " + cfg.dexcomServer, 20, 195, 1);
    _spr.drawString("User: " + (cfg.dexcomUser.length() > 0 ? cfg.dexcomUser.substring(0, 8) + "..." : "Not set"), 20, 215, 1);
    _spr.drawString("Timezone: GMT+" + String(cfg.timezoneOffset), 20, 235, 1);
    _spr.drawString("IP: " + WiFi.localIP().toString(), 20, 255, 1);

    _spr.pushSprite(0, 0);
}
