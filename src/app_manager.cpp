#include "app_manager.h"
#include "web_server_manager.h"
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

    // Set initial brightness
    setBrightness(cfg.screenBrightness);

    DexcomClient::getInstance().setCredentials(
        cfg.dexcomUser, cfg.dexcomPass, cfg.dexcomServer
    );

    Serial.println("[AppManager] Initializing WiFi...");
    initWiFi();

    Serial.println("[AppManager] Launching Main Menu...");
    switchTo(AppState::Launcher);
}

void AppManager::setBrightness(int val) {
    val = constrain(val, 10, 255);
    ConfigManager::getInstance().getConfig().screenBrightness = val;
    ledcWrite(0, val);
}

void AppManager::triggerImmediateCgmFetch() {
    if (_cgmTaskHandle) {
        xTaskNotifyGive(_cgmTaskHandle);
    }
}

String AppManager::getCurrentStateName() const {
    switch (_currentState) {
        case AppState::Launcher: return "Launcher";
        case AppState::Clock: return "Clock";
        case AppState::CGM: return "Dexcom";
        case AppState::Settings: return "Settings";
        default: return "Unknown";
    }
}

void AppManager::toggleCgmView() {
    _cgmViewMode = static_cast<CgmViewMode>((static_cast<int>(_cgmViewMode) + 1) % 3);
    if (_currentState == AppState::CGM) {
        drawCGM();
    }
}

void AppManager::setCgmView(CgmViewMode mode) {
    _cgmViewMode = mode;
    if (_currentState == AppState::CGM) {
        drawCGM();
    }
}

void AppManager::toggleCgmOrientation() {
    _cgmHorizontal = !_cgmHorizontal;
    if (_currentState == AppState::CGM) {
        setScreenOrientation(_cgmHorizontal);
        drawCGM();
    }
}

void AppManager::toggleClockFace() {
    _clockFaceMode = static_cast<ClockFaceMode>((static_cast<int>(_clockFaceMode) + 1) % 3);
    if (_currentState == AppState::Clock) {
        drawClock();
    }
}

void AppManager::setClockFace(ClockFaceMode mode) {
    _clockFaceMode = mode;
    if (_currentState == AppState::Clock) {
        drawClock();
    }
}

void AppManager::toggleClockOrientation() {
    _clockHorizontal = !_clockHorizontal;
    if (_currentState == AppState::Clock) {
        setScreenOrientation(_clockHorizontal);
        drawClock();
    }
}

void AppManager::initDisplay() {
    // 1. Enable power to LCD
    pinMode(PIN_POWER_ON, OUTPUT);
    digitalWrite(PIN_POWER_ON, HIGH);
    delay(100);

    // 2. Initialize TFT_eSPI
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

    // 3. Setup Backlight PWM channel (MUST be after _tft.begin to prevent digital override)
    pinMode(PIN_LCD_BL, OUTPUT);
    ledcSetup(0, 5000, 8);
    ledcAttachPin(PIN_LCD_BL, 0);
    ledcWrite(0, 200);

    // 4. Create full screen sprite for flicker-free double buffering
    _spr.setColorDepth(16);
    _spr.createSprite(SCREEN_WIDTH, SCREEN_HEIGHT);
    Serial.println("[Display] TFT ST7789 initialized, PWM backlight attached, & double buffer created.");
}

void AppManager::initButtons() {
    pinMode(PIN_BUTTON_1, INPUT_PULLUP);
    pinMode(PIN_BUTTON_2, INPUT_PULLUP);
    Serial.println("[Buttons] Direct GPIO mode configured for Pin 0 (BOOT) and Pin 14 (KEY1).");
}

static unsigned long s_lastCgmFetchTime = 0;

static void cgmBackgroundTask(void* param) {
    while (true) {
        CgmReading reading;
        std::vector<int> history;
        Serial.println("[CGM Task] Fetching reading in background...");
        s_lastCgmFetchTime = millis();
        DexcomClient::getInstance().fetchLatestReading(reading, history);
        s_lastCgmFetchTime = millis();
        Serial.printf("[CGM Task] Done. Valid: %d, Val: %d\n", reading.isValid, reading.value);
        // Sleep for 60 seconds or wake early if triggered from web dashboard
        ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(60000));
    }
}

void AppManager::setScreenOrientation(bool horizontal) {
    _cgmHorizontal = horizontal;
    _spr.deleteSprite();
    if (horizontal) {
        _tft.setRotation(1); // Landscape (320x170)
        _spr.createSprite(SCREEN_HEIGHT, SCREEN_WIDTH); // 320 x 170
    } else {
        _tft.setRotation(0); // Portrait (170x320)
        _spr.createSprite(SCREEN_WIDTH, SCREEN_HEIGHT); // 170 x 320
    }
}

void AppManager::switchTo(AppState newState) {
    // If leaving CGM or Clock or switching to any other app, ensure correct orientation
    if (newState == AppState::Clock) {
        setScreenOrientation(_clockHorizontal);
    } else if (newState == AppState::CGM) {
        setScreenOrientation(_cgmHorizontal);
    } else {
        // Launcher, Settings, etc. are always portrait
        if (_cgmHorizontal || _clockHorizontal) {
            setScreenOrientation(false);
        }
    }

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
        _settingsItemIndex = 0;
        drawSettings();
    }
}

void AppManager::initWiFi() {
    WiFi.mode(WIFI_STA);

    _spr.fillSprite(COLOR_BG);
    _spr.setTextColor(COLOR_TEXT_WHITE, COLOR_BG);
    _spr.setTextDatum(MC_DATUM);
    _spr.setFreeFont(&FreeSansBold9pt7b);
    _spr.drawString("Connecting WiFi...", SCREEN_WIDTH / 2, SCREEN_HEIGHT / 2);
    _spr.setTextFont(1);
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

        // Start Web Server
        WebServerManager::getInstance().begin();
    } else {
        Serial.println("[WiFi] Could not connect. Launching Config Portal AP...");
        launchAPPortal();
    }
}

void AppManager::launchAPPortal() {
    Serial.println("\n>>> [AP Mode] Starting 'T-Display AP' Access Point <<<");
    _spr.fillSprite(COLOR_BG);

    // Status Banner / Icon
    _spr.fillRoundRect(SCREEN_WIDTH / 2 - 24, 28, 48, 48, 24, 0x0A24);
    _spr.drawRoundRect(SCREEN_WIDTH / 2 - 24, 28, 48, 48, 24, COLOR_ACCENT);
    _spr.setTextColor(COLOR_ACCENT, 0x0A24);
    _spr.setTextDatum(MC_DATUM);
    _spr.setFreeFont(&FreeSansBold12pt7b);
    _spr.drawString("AP", SCREEN_WIDTH / 2, 52);

    // Main Header
    _spr.setTextColor(COLOR_TEXT_WHITE, COLOR_BG);
    _spr.setTextDatum(TC_DATUM);
    _spr.setFreeFont(&FreeSansBold9pt7b);
    _spr.drawString("Device in AP Mode", SCREEN_WIDTH / 2, 88);

    // Instruction subtitle
    _spr.setTextColor(COLOR_TEXT_MUTED, COLOR_BG);
    _spr.setTextFont(2);
    _spr.drawString("Connect to configure", SCREEN_WIDTH / 2, 114);
    _spr.drawString("device settings", SCREEN_WIDTH / 2, 132);

    // Clean Centered Card for WiFi SSID & Web Address (y=164, h=96)
    int cardX = 12;
    int cardW = SCREEN_WIDTH - 24; // 146px
    _spr.fillRoundRect(cardX, 164, cardW, 98, 10, COLOR_CARD_BG);
    _spr.drawRoundRect(cardX, 164, cardW, 98, 10, 0x39E7);

    // Wi-Fi SSID
    _spr.setTextColor(COLOR_TEXT_MUTED, COLOR_CARD_BG);
    _spr.setTextDatum(TC_DATUM);
    _spr.setTextFont(1);
    _spr.drawString("WI-FI NETWORK:", SCREEN_WIDTH / 2, 174);

    _spr.setTextColor(COLOR_YELLOW, COLOR_CARD_BG);
    _spr.setFreeFont(&FreeSansBold9pt7b);
    _spr.drawString("T-Display AP", SCREEN_WIDTH / 2, 192);

    // Divider
    _spr.drawFastHLine(cardX + 16, 215, cardW - 32, 0x3186);

    // Web Address
    _spr.setTextColor(COLOR_TEXT_MUTED, COLOR_CARD_BG);
    _spr.setTextDatum(TC_DATUM);
    _spr.setTextFont(1);
    _spr.drawString("BROWSER ADDRESS:", SCREEN_WIDTH / 2, 222);

    _spr.setTextColor(COLOR_GREEN, COLOR_CARD_BG);
    _spr.setFreeFont(&FreeSansBold9pt7b);
    _spr.drawString("192.168.4.1", SCREEN_WIDTH / 2, 240);

    _spr.setTextFont(1);
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
        // Show saved message and reboot so clean state and tasks start fresh
        _spr.fillSprite(COLOR_BG);
        _spr.setTextColor(COLOR_GREEN, COLOR_BG);
        _spr.setTextDatum(MC_DATUM);
        _spr.setFreeFont(&FreeSansBold12pt7b);
        _spr.drawString("Settings Saved!", SCREEN_WIDTH / 2, SCREEN_HEIGHT / 2 - 20);
        _spr.setTextColor(COLOR_TEXT_MUTED, COLOR_BG);
        _spr.setFreeFont(&FreeSans9pt7b);
        _spr.drawString("Restarting device...", SCREEN_WIDTH / 2, SCREEN_HEIGHT / 2 + 20);
        _spr.setTextFont(1);
        _spr.pushSprite(0, 0);
        delay(1500);
        ESP.restart();
    } else {
        Serial.println("[WiFiManager] Config portal timed out or cancelled.");
        switchTo(AppState::Settings);
    }
}

void AppManager::factoryReset() {
    _spr.fillSprite(COLOR_BG);
    _spr.setTextColor(COLOR_RED, COLOR_BG);
    _spr.setTextDatum(MC_DATUM);
    _spr.setFreeFont(&FreeSansBold12pt7b);
    _spr.drawString("Factory Resetting...", SCREEN_WIDTH / 2, SCREEN_HEIGHT / 2);
    _spr.setTextFont(1);
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
    } else if (_currentState == AppState::Clock) {
        // B1: Toggle between watch faces (Modern -> BoldDigital -> ClassicClean -> Modern)
        _clockFaceMode = static_cast<ClockFaceMode>((static_cast<int>(_clockFaceMode) + 1) % 3);
        drawClock();
    } else if (_currentState == AppState::Settings) {
        _settingsItemIndex = (_settingsItemIndex + 1) % 2; // Toggle between 0 (Set Device) and 1 (Information)
        drawSettings();
    } else if (_currentState == AppState::CGM) {
        // Toggle view: ValueAndGraph -> ValueFullScreen -> GraphFullScreen -> ValueAndGraph
        _cgmViewMode = static_cast<CgmViewMode>((static_cast<int>(_cgmViewMode) + 1) % 3);
        drawCGM();
    }
}

void AppManager::onBtn2Click() {
    if (_currentState == AppState::Launcher) {
        if (_selectedAppIndex == 0) switchTo(AppState::Clock);
        else if (_selectedAppIndex == 1) switchTo(AppState::CGM);
        else if (_selectedAppIndex == 2) switchTo(AppState::Settings);
    } else if (_currentState == AppState::Clock) {
        // B2: Rotate vertical <-> horizontal
        _clockHorizontal = !_clockHorizontal;
        setScreenOrientation(_clockHorizontal);
        drawClock();
    } else if (_currentState == AppState::Settings) {
        if (_settingsItemIndex == 0) {
            // 1. Set Device (AP) selected -> Launch AP Portal & reboot upon completion
            launchAPPortal();
        }
    } else if (_currentState == AppState::CGM) {
        // Rotate: Vertical <-> Horizontal
        setScreenOrientation(!_cgmHorizontal);
        drawCGM();
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
    if (b1Current != _b1Prev) {
        _b1DownTime = now;
    }
    if ((now - _b1DownTime) > 35) { // 35ms debounce threshold
        if (b1Current && !_b1Handled && !_chordHandled) {
            _b1Handled = true;
            Serial.println("[Buttons] Button 1 CLICK!");
            onBtn1Click();
        } else if (!b1Current) {
            _b1Handled = false;
        }
    }
    _b1Prev = b1Current;

    // 3. Button 2 (IO14) Debounce & Click Detection
    if (b2Current != _b2Prev) {
        _b2DownTime = now;
    }
    if ((now - _b2DownTime) > 35) {
        if (b2Current && !_b2Handled && !_chordHandled) {
            _b2Handled = true;
            Serial.println("[Buttons] Button 2 CLICK!");
            onBtn2Click();
        } else if (!b2Current) {
            _b2Handled = false;
        }
    }
    _b2Prev = b2Current;

    // Service Web Server requests
    WebServerManager::getInstance().handleClient();

    // App periodic UI updates
    if (_currentState == AppState::Clock) {
        updateTime();
    } else if (_currentState == AppState::CGM) {
        updateCGM();
    }
}

void AppManager::drawLauncher() {
    _spr.fillSprite(COLOR_BG);

    // Header with smooth FreeSansBold9
    _spr.setTextColor(COLOR_ACCENT, COLOR_BG);
    _spr.setTextDatum(TC_DATUM);
    _spr.setFreeFont(&FreeSansBold9pt7b);
    _spr.drawString("T-DISPLAY S3", SCREEN_WIDTH / 2, 14);

    // App Cards
    const char* appNames[APP_COUNT] = {"Clock", "Dexcom", "Settings"};
    const char* appDescs[APP_COUNT] = {"Digital NTP Clock", "Live Blood Glucose", "WiFi & Preferences"};

    int cardWidth = 150;
    int cardHeight = 62;
    int startY = 46;
    int spacing = 16;

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

        // App Title (FreeSansBold9pt7b)
        _spr.setTextDatum(TL_DATUM);
        _spr.setTextColor(isSelected ? COLOR_TEXT_WHITE : 0xC618, bgCol);
        _spr.setFreeFont(&FreeSansBold9pt7b);
        _spr.drawString(appNames[i], 22, y + 12);

        // App Subtitle / Description (Clean Font 2)
        _spr.setTextColor(isSelected ? COLOR_ACCENT : COLOR_TEXT_MUTED, bgCol);
        _spr.setTextFont(2);
        _spr.drawString(appDescs[i], 22, y + 36);
    }

    // Bottom Navigation Bar - Aligned with bottom physical buttons (Font 2)
    _spr.drawFastHLine(10, SCREEN_HEIGHT - 32, SCREEN_WIDTH - 20, COLOR_CARD_BG);
    _spr.setTextFont(2);
    
    // Left: B1 (BOOT button) -> Next
    _spr.setTextDatum(BL_DATUM);
    _spr.setTextColor(COLOR_ACCENT, COLOR_BG);
    _spr.drawString("B1", 10, SCREEN_HEIGHT - 8);
    _spr.setTextColor(COLOR_TEXT_MUTED, COLOR_BG);
    _spr.drawString(": Next", 28, SCREEN_HEIGHT - 8);

    // Right: B2 (IO14 button) -> Select
    _spr.setTextDatum(BR_DATUM);
    _spr.setTextColor(COLOR_TEXT_MUTED, COLOR_BG);
    _spr.drawString("Select : ", SCREEN_WIDTH - 28, SCREEN_HEIGHT - 8);
    _spr.setTextColor(COLOR_GREEN, COLOR_BG);
    _spr.drawString("B2", SCREEN_WIDTH - 10, SCREEN_HEIGHT - 8);

    _spr.setTextFont(1);
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

    if (_clockHorizontal) {
        drawClockHorizontal(SCREEN_HEIGHT, SCREEN_WIDTH, timeinfo); // 320 x 170
    } else {
        drawClockVertical(SCREEN_WIDTH, SCREEN_HEIGHT, timeinfo);   // 170 x 320
    }

    _spr.pushSprite(0, 0);
}

void AppManager::drawClockVertical(int w, int h, const struct tm& timeinfo) {
    AppConfig& cfg = ConfigManager::getInstance().getConfig();

    // 1. Top status line: WiFi indicator & Watch face badge
    _spr.setTextFont(2);
    _spr.setTextColor(COLOR_TEXT_MUTED, COLOR_BG);
    _spr.setTextDatum(TL_DATUM);
    _spr.drawString((WiFi.status() == WL_CONNECTED) ? "WiFi: OK" : "WiFi: DIS", 8, 6);

    // Watch Face Name Badge top-right
    const char* faceNames[] = {"Modern", "Bold", "Minimal"};
    _spr.setTextDatum(TR_DATUM);
    _spr.setTextColor(COLOR_ACCENT, COLOR_BG);
    _spr.drawString(faceNames[static_cast<int>(_clockFaceMode)], w - 8, 6);

    // 2. Navigation bar at bottom of screen: B1 (Toggle Face) & B2 (Rotate)
    _spr.drawFastHLine(10, h - 28, w - 20, COLOR_CARD_BG);
    _spr.setTextFont(2);

    _spr.setTextDatum(BL_DATUM);
    _spr.setTextColor(COLOR_ACCENT, COLOR_BG);
    _spr.drawString("B1", 10, h - 6);
    _spr.setTextColor(COLOR_TEXT_MUTED, COLOR_BG);
    _spr.drawString(": Face", 28, h - 6);

    _spr.setTextDatum(BR_DATUM);
    _spr.setTextColor(COLOR_TEXT_MUTED, COLOR_BG);
    _spr.drawString("Rot : ", w - 26, h - 6);
    _spr.setTextColor(COLOR_GREEN, COLOR_BG);
    _spr.drawString("B2", w - 10, h - 6);

    // Formatting buffers
    char timeBuf[16];
    char secBuf[8];
    char dateBuf[32];
    char dayBuf[16];

    if (cfg.use24HourFormat) {
        strftime(timeBuf, sizeof(timeBuf), "%H:%M", &timeinfo);
    } else {
        strftime(timeBuf, sizeof(timeBuf), "%I:%M", &timeinfo);
    }
    strftime(secBuf, sizeof(secBuf), "%S", &timeinfo); // NO colon! Just pure seconds
    strftime(dayBuf, sizeof(dayBuf), "%A", &timeinfo);
    strftime(dateBuf, sizeof(dateBuf), "%b %d, %Y", &timeinfo);

    int secVal = timeinfo.tm_sec; // 0..59

    // =========================================================================
    // FACE 1: MODERN DIGITAL (Digital readout with massive seconds filling lower screen)
    // =========================================================================
    if (_clockFaceMode == ClockFaceMode::Modern) {
        // Day & Date in vector FreeSans
        _spr.setTextDatum(TC_DATUM);
        _spr.setTextColor(COLOR_ACCENT, COLOR_BG);
        _spr.setFreeFont(&FreeSansBold9pt7b);
        _spr.drawString(dayBuf, w / 2, 34);

        _spr.setTextColor(COLOR_TEXT_MUTED, COLOR_BG);
        _spr.setTextFont(2);
        _spr.drawString(dateBuf, w / 2, 58);

        // Digital Time (Font 7 = crisp 7-segment digital font)
        _spr.setTextColor(COLOR_TEXT_WHITE, COLOR_BG);
        _spr.setTextDatum(TC_DATUM);
        _spr.drawString(timeBuf, w / 2, 86, 7);

        // Dynamic 60-second progress bar
        int barW = w - 24;
        int barFill = (secVal * barW) / 59;
        _spr.fillRoundRect(12, 144, barW, 4, 2, COLOR_CARD_BG);
        _spr.fillRoundRect(12, 144, max(4, barFill), 4, 2, COLOR_ACCENT);

        // Giant Seconds Display filling the bottom area above navigation bar
        _spr.fillRoundRect(10, 156, w - 20, 126, 12, COLOR_CARD_BG);
        _spr.drawRoundRect(10, 156, w - 20, 126, 12, 0x39E7);

        _spr.setTextColor(COLOR_TEXT_MUTED, COLOR_CARD_BG);
        _spr.setTextDatum(TC_DATUM);
        _spr.setTextFont(1);
        _spr.drawString("SECONDS", w / 2, 166);

        // Enormous Font 8 numerals (clean, bold, beautiful, fills frame)
        _spr.setTextColor(COLOR_ACCENT, COLOR_CARD_BG);
        _spr.setTextDatum(MC_DATUM);
        _spr.drawString(secBuf, w / 2, 226, 8);
    }
    // =========================================================================
    // FACE 2: BOLD DIGITAL (Inspired by stopwatch big number watch face)
    // =========================================================================
    else if (_clockFaceMode == ClockFaceMode::BoldDigital) {
        char hrBuf[8];
        char minBuf[8];
        if (cfg.use24HourFormat) {
            strftime(hrBuf, sizeof(hrBuf), "%H", &timeinfo);
        } else {
            strftime(hrBuf, sizeof(hrBuf), "%I", &timeinfo);
        }
        strftime(minBuf, sizeof(minBuf), "%M", &timeinfo);

        // Stacked massive numbers for Hours and Minutes
        _spr.setTextDatum(TC_DATUM);
        _spr.setTextColor(COLOR_TEXT_WHITE, COLOR_BG);
        _spr.drawString(hrBuf, w / 2, 28, 7); // Big Hour

        _spr.setTextColor(COLOR_ACCENT, COLOR_BG);
        _spr.drawString(minBuf, w / 2, 86, 7); // Big Minute

        // Date chip
        _spr.fillRoundRect(22, 144, w - 44, 22, 11, COLOR_CARD_BG);
        _spr.setTextColor(COLOR_TEXT_MUTED, COLOR_CARD_BG);
        _spr.setTextDatum(MC_DATUM);
        _spr.setTextFont(2);
        _spr.drawString(String(dayBuf).substring(0, 3) + "  " + dateBuf, w / 2, 155);

        // Massive Seconds Card filling lower screen
        _spr.fillRoundRect(10, 174, w - 20, 108, 12, 0x0A24);
        _spr.drawRoundRect(10, 174, w - 20, 108, 12, COLOR_ACCENT);

        _spr.setTextColor(COLOR_YELLOW, 0x0A24);
        _spr.setTextDatum(MC_DATUM);
        _spr.drawString(secBuf, w / 2, 228, 8); // Huge font 8 seconds
    }
    // =========================================================================
    // FACE 3: MINIMAL CLEAN (Orbital Second ring, elegant typography)
    // =========================================================================
    else if (_clockFaceMode == ClockFaceMode::ClassicClean) {
        // Date header
        _spr.setTextDatum(TC_DATUM);
        _spr.setTextColor(COLOR_TEXT_MUTED, COLOR_BG);
        _spr.setTextFont(2);
        _spr.drawString(String(dayBuf) + ", " + dateBuf, w / 2, 34);

        // Time in clean Font 7
        _spr.setTextColor(COLOR_TEXT_WHITE, COLOR_BG);
        _spr.drawString(timeBuf, w / 2, 68, 7);

        // Circular seconds orbit dial in center-lower screen
        int dialCx = w / 2;
        int dialCy = 186;
        int dialR = 48;

        _spr.drawCircle(dialCx, dialCy, dialR, COLOR_CARD_BG);
        _spr.drawCircle(dialCx, dialCy, dialR - 1, 0x2124);

        // Second tick marks at 12, 3, 6, 9 o'clock
        _spr.drawFastVLine(dialCx, dialCy - dialR, 6, COLOR_TEXT_MUTED);
        _spr.drawFastVLine(dialCx, dialCy + dialR - 6, 6, COLOR_TEXT_MUTED);
        _spr.drawFastHLine(dialCx - dialR, dialCy, 6, COLOR_TEXT_MUTED);
        _spr.drawFastHLine(dialCx + dialR - 6, dialCy, 6, COLOR_TEXT_MUTED);

        // Orbital second ball / pointer
        float angle = (secVal * 6.0f - 90.0f) * 0.0174532925f; // Radian
        int ballX = dialCx + (int)(cos(angle) * (dialR - 4));
        int ballY = dialCy + (int)(sin(angle) * (dialR - 4));
        _spr.fillCircle(ballX, ballY, 4, COLOR_ACCENT);

        // Huge second numeral inside dial
        _spr.setTextColor(COLOR_GREEN, COLOR_BG);
        _spr.setTextDatum(MC_DATUM);
        _spr.drawString(secBuf, dialCx, dialCy, 6); // Large font 6 digits

        _spr.setTextColor(COLOR_TEXT_MUTED, COLOR_BG);
        _spr.setTextFont(1);
        _spr.drawString("SEC", dialCx, dialCy + 26);
    }

    _spr.setTextFont(1);
}

void AppManager::drawClockHorizontal(int w, int h, const struct tm& timeinfo) {
    AppConfig& cfg = ConfigManager::getInstance().getConfig();

    // 1. Top status line: WiFi (left), Face name (center), Rotate B2 (top right)
    _spr.setTextFont(2);
    _spr.setTextDatum(TL_DATUM);
    _spr.setTextColor(COLOR_TEXT_MUTED, COLOR_BG);
    _spr.drawString((WiFi.status() == WL_CONNECTED) ? "WiFi: OK" : "WiFi: DIS", 8, 6);

    const char* faceNames[] = {"Modern", "Bold", "Minimal"};
    _spr.setTextDatum(TC_DATUM);
    _spr.setTextColor(COLOR_ACCENT, COLOR_BG);
    _spr.drawString(faceNames[static_cast<int>(_clockFaceMode)], w / 2 - 20, 6);

    _spr.setTextDatum(TR_DATUM);
    _spr.setTextColor(COLOR_TEXT_MUTED, COLOR_BG);
    _spr.drawString("Rotate : ", w - 26, 6);
    _spr.setTextColor(COLOR_GREEN, COLOR_BG);
    _spr.drawString("B2", w - 8, 6);

    // Bottom Navigation Bar: "Face : B1" (bottom right)
    _spr.drawFastHLine(10, h - 24, w - 20, COLOR_CARD_BG);
    _spr.setTextFont(2);
    _spr.setTextDatum(BR_DATUM);
    _spr.setTextColor(COLOR_TEXT_MUTED, COLOR_BG);
    _spr.drawString("Face : ", w - 26, h - 4);
    _spr.setTextColor(COLOR_ACCENT, COLOR_BG);
    _spr.drawString("B1", w - 8, h - 4);

    // Formatting buffers
    char timeBuf[16];
    char secBuf[8];
    char dateBuf[32];
    char dayBuf[16];

    if (cfg.use24HourFormat) {
        strftime(timeBuf, sizeof(timeBuf), "%H:%M", &timeinfo);
    } else {
        strftime(timeBuf, sizeof(timeBuf), "%I:%M", &timeinfo);
    }
    strftime(secBuf, sizeof(secBuf), "%S", &timeinfo); // NO colon
    strftime(dayBuf, sizeof(dayBuf), "%A", &timeinfo);
    strftime(dateBuf, sizeof(dateBuf), "%b %d, %Y", &timeinfo);

    int secVal = timeinfo.tm_sec;

    // Split screen: Left side Time & Date (width 190), Right side Big Seconds Card (width 110)
    _spr.setTextColor(COLOR_TEXT_WHITE, COLOR_BG);
    _spr.setTextDatum(TC_DATUM);
    _spr.drawString(timeBuf, 95, 30, 7); // Big Font 7 digital time

    _spr.setTextDatum(TC_DATUM);
    _spr.setTextColor(COLOR_ACCENT, COLOR_BG);
    _spr.setFreeFont(&FreeSansBold9pt7b);
    _spr.drawString(dayBuf, 95, 88);

    _spr.setTextColor(COLOR_TEXT_MUTED, COLOR_BG);
    _spr.setTextFont(2);
    _spr.drawString(dateBuf, 95, 112);

    // Right side: Massive Seconds Card (x=195, y=26, w=115, h=110)
    _spr.fillRoundRect(195, 26, 115, 110, 12, COLOR_CARD_BG);
    _spr.drawRoundRect(195, 26, 115, 110, 12, (_clockFaceMode == ClockFaceMode::BoldDigital) ? COLOR_YELLOW : COLOR_ACCENT);

    _spr.setTextColor(COLOR_TEXT_MUTED, COLOR_CARD_BG);
    _spr.setTextDatum(TC_DATUM);
    _spr.setTextFont(1);
    _spr.drawString("SECONDS", 252, 34);

    uint16_t secCol = COLOR_ACCENT;
    if (_clockFaceMode == ClockFaceMode::BoldDigital) secCol = COLOR_YELLOW;
    else if (_clockFaceMode == ClockFaceMode::ClassicClean) secCol = COLOR_GREEN;

    _spr.setTextColor(secCol, COLOR_CARD_BG);
    _spr.setTextDatum(MC_DATUM);
    _spr.drawString(secBuf, 252, 85, 8); // Huge Font 8 seconds filling right side

    _spr.setTextFont(1);
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

    // Calculate sync countdown seconds (60 second period)
    int syncSecRemaining = 0;
    if (s_lastCgmFetchTime > 0) {
        long elapsed = (millis() - s_lastCgmFetchTime) / 1000;
        syncSecRemaining = max(0L, 60L - (elapsed % 60));
    }

    if (_cgmHorizontal) {
        drawCGMHorizontal(SCREEN_HEIGHT, SCREEN_WIDTH, cgm, history, syncSecRemaining); // 320 x 170
    } else {
        drawCGMVertical(SCREEN_WIDTH, SCREEN_HEIGHT, cgm, history, syncSecRemaining);   // 170 x 320
    }

    _spr.pushSprite(0, 0);
}

void AppManager::drawTrendArrow(int cx, int cy, CgmTrend trend, uint16_t color, int size) {
    // Vector arrow drawing based on official Dexcom trend direction
    int s = size;
    int s2 = size / 2;
    int s3 = size / 3;

    auto drawSingleArrow = [&](int ox, int oy, int dx, int dy) {
        // dx, dy are directional steps:
        // dx=1, dy=0 (Flat)
        // dx=1, dy=-1 (45 Up)
        // dx=0, dy=-1 (Up)
        // dx=1, dy=1 (45 Down)
        // dx=0, dy=1 (Down)
        if (dx == 0 && dy == -1) { // Up
            _spr.fillTriangle(ox, oy - s, ox - s2, oy, ox + s2, oy, color);
            _spr.fillRect(ox - 2, oy, 5, s, color);
        } else if (dx == 0 && dy == 1) { // Down
            _spr.fillTriangle(ox, oy + s, ox - s2, oy, ox + s2, oy, color);
            _spr.fillRect(ox - 2, oy - s, 5, s, color);
        } else if (dx == 1 && dy == 0) { // Flat (Right)
            _spr.fillTriangle(ox + s, oy, ox, oy - s2, ox, oy + s2, color);
            _spr.fillRect(ox - s, oy - 2, s, 5, color);
        } else if (dx == 1 && dy == -1) { // FortyFiveUp (Diagonal up-right)
            // Head pointing 45 degrees up-right
            int hx = ox + (int)(s * 0.7f);
            int hy = oy - (int)(s * 0.7f);
            _spr.fillTriangle(hx, hy, hx - s, hy, hx, hy + s, color);
            // Thick stem
            _spr.drawLine(ox - s2, oy + s2, hx, hy, color);
            _spr.drawLine(ox - s2 + 1, oy + s2, hx + 1, hy, color);
            _spr.drawLine(ox - s2 - 1, oy + s2, hx - 1, hy, color);
            _spr.drawLine(ox - s2, oy + s2 + 1, hx, hy + 1, color);
            _spr.drawLine(ox - s2, oy + s2 - 1, hx, hy - 1, color);
        } else if (dx == 1 && dy == 1) { // FortyFiveDown (Diagonal down-right)
            int hx = ox + (int)(s * 0.7f);
            int hy = oy + (int)(s * 0.7f);
            _spr.fillTriangle(hx, hy, hx - s, hy, hx, hy - s, color);
            // Thick stem
            _spr.drawLine(ox - s2, oy - s2, hx, hy, color);
            _spr.drawLine(ox - s2 + 1, oy - s2, hx + 1, hy, color);
            _spr.drawLine(ox - s2 - 1, oy - s2, hx - 1, hy, color);
            _spr.drawLine(ox - s2, oy - s2 + 1, hx, hy + 1, color);
            _spr.drawLine(ox - s2, oy - s2 - 1, hx, hy - 1, color);
        }
    };

    switch (trend) {
        case CgmTrend::DoubleUp:
            drawSingleArrow(cx - s3 - 2, cy, 0, -1);
            drawSingleArrow(cx + s3 + 2, cy, 0, -1);
            break;
        case CgmTrend::SingleUp:
            drawSingleArrow(cx, cy, 0, -1);
            break;
        case CgmTrend::FortyFiveUp:
            drawSingleArrow(cx, cy, 1, -1);
            break;
        case CgmTrend::Flat:
            drawSingleArrow(cx, cy, 1, 0);
            break;
        case CgmTrend::FortyFiveDown:
            drawSingleArrow(cx, cy, 1, 1);
            break;
        case CgmTrend::SingleDown:
            drawSingleArrow(cx, cy, 0, 1);
            break;
        case CgmTrend::DoubleDown:
            drawSingleArrow(cx - s3 - 2, cy, 0, 1);
            drawSingleArrow(cx + s3 + 2, cy, 0, 1);
            break;
        default:
            _spr.fillRect(cx - s2, cy - 2, s, 4, color);
            break;
    }
}

void AppManager::drawCGMVertical(int w, int h, const CgmReading& cgm, const std::vector<int>& history, int syncSecRemaining) {
    // 1. Top status line: "Wifi: OK/FAIL" (left) & "Sync in: [sec]" (right)
    _spr.setTextFont(2);
    _spr.setTextDatum(TL_DATUM);
    if (WiFi.status() == WL_CONNECTED) {
        _spr.setTextColor(COLOR_GREEN, COLOR_BG);
        _spr.drawString("Wifi: OK", 8, 8);
    } else {
        _spr.setTextColor(COLOR_RED, COLOR_BG);
        _spr.drawString("Wifi: FAIL", 8, 8);
    }

    _spr.setTextDatum(TR_DATUM);
    _spr.setTextColor(COLOR_TEXT_MUTED, COLOR_BG);
    _spr.drawString("Sync: " + String(syncSecRemaining) + "s", w - 8, 8);

    // Bottom Navigation Bar: "B1: Mode" (left) & "Rot: B2" (right) in Font 2
    _spr.drawFastHLine(10, h - 30, w - 20, COLOR_CARD_BG);
    _spr.setTextFont(2);
    _spr.setTextDatum(BL_DATUM);
    _spr.setTextColor(COLOR_ACCENT, COLOR_BG);
    _spr.drawString("B1", 10, h - 8);
    _spr.setTextColor(COLOR_TEXT_MUTED, COLOR_BG);
    _spr.drawString(": Mode", 28, h - 8);

    _spr.setTextDatum(BR_DATUM);
    _spr.setTextColor(COLOR_TEXT_MUTED, COLOR_BG);
    _spr.drawString("Rot : ", w - 26, h - 8);
    _spr.setTextColor(COLOR_GREEN, COLOR_BG);
    _spr.drawString("B2", w - 10, h - 8);

    if (!cgm.isValid) {
        _spr.setTextColor(COLOR_YELLOW, COLOR_BG);
        _spr.setTextDatum(MC_DATUM);
        _spr.setFreeFont(&FreeSansBold9pt7b);
        _spr.drawString(cgm.statusMessage, w / 2, h / 2 - 20);
        _spr.setTextColor(COLOR_TEXT_MUTED, COLOR_BG);
        _spr.setFreeFont(&FreeSans9pt7b);
        _spr.drawString("Fetching Dexcom...", w / 2, h / 2 + 10);
        _spr.setTextFont(1);
        return;
    }

    // Determine glucose range color using configured targets
    AppConfig& cfg = ConfigManager::getInstance().getConfig();
    int tgtLow = cfg.targetLow > 0 ? cfg.targetLow : 70;
    int tgtHigh = cfg.targetHigh > 0 ? cfg.targetHigh : 180;

    uint16_t valColor = COLOR_GREEN;
    if (cgm.value < tgtLow || cgm.value > 250) valColor = COLOR_RED;
    else if (cgm.value > tgtHigh) valColor = COLOR_YELLOW;

    String trendSym = "--";
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
    String ageStr = (cgm.ageMinutes <= 1) ? "Just now" : (String(cgm.ageMinutes) + "m ago");

    // -----------------------------------------------------------
    // View 1: Value + Graph (Vertical)
    // -----------------------------------------------------------
    if (_cgmViewMode == CgmViewMode::ValueAndGraph) {
        // Value (Font 7 = 7-segment digital font)
        _spr.setTextColor(valColor, COLOR_BG);
        _spr.setTextDatum(TC_DATUM);
        _spr.drawString(String(cgm.value), w / 2, 28, 7);

        // Delta and Vector Arrow
        // Draw delta text centered slightly left, and vector arrow right next to it
        _spr.setTextColor(COLOR_TEXT_WHITE, COLOR_BG);
        _spr.setFreeFont(&FreeSansBold9pt7b);
        _spr.setTextDatum(MR_DATUM);
        _spr.drawString("mg/dL  " + deltaStr, w / 2 + 10, 94);
        drawTrendArrow(w / 2 + 28, 94, cgm.trend, valColor, 10);

        // Age (Font 2)
        _spr.setTextColor(COLOR_TEXT_MUTED, COLOR_BG);
        _spr.setTextFont(2);
        _spr.setTextDatum(TC_DATUM);
        _spr.drawString(ageStr, w / 2, 118);

        // Graph
        int gx = 10;
        int gy = 144;
        int gw = w - 20;
        int gh = 106;
        _spr.fillRoundRect(gx, gy, gw, gh, 6, COLOR_CARD_BG);
        _spr.drawRoundRect(gx, gy, gw, gh, 6, 0x39E7);

        auto mapY = [&](int val) {
            int cl = constrain(val, 40, 300);
            return gy + gh - (int)((cl - 40) * (float)gh / 260.0f);
        };
        _spr.drawFastHLine(gx + 2, mapY(tgtLow), gw - 4, 0x8000);
        _spr.drawFastHLine(gx + 2, mapY(tgtHigh), gw - 4, 0x8400);

        if (history.size() > 1) {
            float stepX = (float)(gw - 16) / (float)(history.size() - 1);
            for (size_t i = 0; i < history.size(); i++) {
                int px = gx + 8 + (int)(i * stepX);
                int py = mapY(history[i]);
                uint16_t ptColor = (history[i] < tgtLow || history[i] > 250) ? COLOR_RED : (history[i] > tgtHigh ? COLOR_YELLOW : COLOR_GREEN);
                _spr.fillCircle(px, py, 2, ptColor);
                if (i > 0) {
                    int prevPx = gx + 8 + (int)((i - 1) * stepX);
                    int prevPy = mapY(history[i - 1]);
                    _spr.drawLine(prevPx, prevPy, px, py, COLOR_TEXT_MUTED);
                }
            }
        }
    }
    // -----------------------------------------------------------
    // View 2: CGM Value Big Full Screen (Vertical)
    // -----------------------------------------------------------
    else if (_cgmViewMode == CgmViewMode::ValueFullScreen) {
        // Big Glucose Reading
        _spr.setTextColor(valColor, COLOR_BG);
        _spr.setTextDatum(MC_DATUM);
        _spr.drawString(String(cgm.value), w / 2, 85, 8); // Huge font 8

        // Units
        _spr.setTextDatum(TC_DATUM);
        _spr.setTextColor(COLOR_TEXT_MUTED, COLOR_BG);
        _spr.setTextFont(2);
        _spr.drawString("mg/dL", w / 2, 130);

        // Prominent Change with explicit +/- badge and vector trend arrow
        uint16_t deltaCol = COLOR_GREEN;
        if (cgm.delta > 0) {
            deltaCol = (cgm.value > tgtHigh) ? COLOR_YELLOW : COLOR_GREEN;
        } else if (cgm.delta < 0) {
            deltaCol = (cgm.value < tgtLow) ? COLOR_RED : COLOR_YELLOW;
        }
        String deltaFull = (cgm.delta > 0 ? "+" : "") + String(cgm.delta);

        _spr.fillRoundRect(12, 156, w - 24, 48, 8, COLOR_CARD_BG);
        _spr.drawRoundRect(12, 156, w - 24, 48, 8, 0x39E7);

        // Delta number on left of badge, vector arrow on right of badge
        _spr.setTextDatum(MR_DATUM);
        _spr.setTextColor(deltaCol, COLOR_CARD_BG);
        _spr.drawString(deltaFull, w / 2 + 5, 180, 6); // Large font 6

        // High quality vector arrow inside badge
        drawTrendArrow(w / 2 + 32, 180, cgm.trend, deltaCol, 14);

        _spr.setTextDatum(TC_DATUM);
        _spr.setTextColor(COLOR_TEXT_MUTED, COLOR_BG);
        _spr.setTextFont(2);
        _spr.drawString(ageStr, w / 2, 222);
    }
    // -----------------------------------------------------------
    // View 3: CGM Graph Full Screen (Vertical)
    // -----------------------------------------------------------
    else if (_cgmViewMode == CgmViewMode::GraphFullScreen) {
        // Quick summary line at top of graph: Glucose, delta, and graphic arrow
        _spr.setTextColor(valColor, COLOR_BG);
        _spr.setTextDatum(MR_DATUM);
        _spr.setFreeFont(&FreeSansBold9pt7b);
        _spr.drawString(String(cgm.value) + " mg/dL (" + deltaStr + ")", w / 2 + 15, 38);
        drawTrendArrow(w / 2 + 32, 38, cgm.trend, valColor, 8);

        int gx = 10;
        int gy = 62;
        int gw = w - 20;
        int gh = h - 105; // Full remaining height
        _spr.fillRoundRect(gx, gy, gw, gh, 6, COLOR_CARD_BG);
        _spr.drawRoundRect(gx, gy, gw, gh, 6, 0x39E7);

        auto mapY = [&](int val) {
            int cl = constrain(val, 40, 300);
            return gy + gh - (int)((cl - 40) * (float)gh / 260.0f);
        };
        _spr.drawFastHLine(gx + 2, mapY(tgtLow), gw - 4, 0x8000);
        _spr.drawFastHLine(gx + 2, mapY(tgtHigh), gw - 4, 0x8400);

        // Threshold labels
        _spr.setTextColor(COLOR_TEXT_MUTED, COLOR_CARD_BG);
        _spr.setTextDatum(TL_DATUM);
        _spr.drawString(String(tgtHigh), gx + 4, mapY(tgtHigh) - 10, 1);
        _spr.drawString(String(tgtLow), gx + 4, mapY(tgtLow) + 2, 1);

        if (history.size() > 1) {
            float stepX = (float)(gw - 16) / (float)(history.size() - 1);
            for (size_t i = 0; i < history.size(); i++) {
                int px = gx + 8 + (int)(i * stepX);
                int py = mapY(history[i]);
                uint16_t ptColor = (history[i] < tgtLow || history[i] > 250) ? COLOR_RED : (history[i] > tgtHigh ? COLOR_YELLOW : COLOR_GREEN);
                _spr.fillCircle(px, py, 3, ptColor);
                if (i > 0) {
                    int prevPx = gx + 8 + (int)((i - 1) * stepX);
                    int prevPy = mapY(history[i - 1]);
                    _spr.drawLine(prevPx, prevPy, px, py, COLOR_TEXT_MUTED);
                }
            }
        }
    }
    _spr.setTextFont(1);
}

void AppManager::drawCGMHorizontal(int w, int h, const CgmReading& cgm, const std::vector<int>& history, int syncSecRemaining) {
    // 1. Top status line: "Wifi: OK/FAIL" (left) & "Sync in: [sec]" (center) & "Rotate : B2" (top right)
    _spr.setTextFont(2);
    _spr.setTextDatum(TL_DATUM);
    if (WiFi.status() == WL_CONNECTED) {
        _spr.setTextColor(COLOR_GREEN, COLOR_BG);
        _spr.drawString("Wifi: OK", 8, 6);
    } else {
        _spr.setTextColor(COLOR_RED, COLOR_BG);
        _spr.drawString("Wifi: FAIL", 8, 6);
    }

    _spr.setTextDatum(TC_DATUM);
    _spr.setTextColor(COLOR_TEXT_MUTED, COLOR_BG);
    _spr.drawString("Sync: " + String(syncSecRemaining) + "s", w / 2 - 20, 6);

    // Top Right: B2 (Rotate) next to physical IO14 button
    _spr.setTextDatum(TR_DATUM);
    _spr.setTextColor(COLOR_TEXT_MUTED, COLOR_BG);
    _spr.drawString("Rotate : ", w - 26, 6);
    _spr.setTextColor(COLOR_GREEN, COLOR_BG);
    _spr.drawString("B2", w - 8, 6);

    // Bottom Navigation Bar: "Toggle : B1" (bottom right) next to physical BOOT button
    _spr.drawFastHLine(10, h - 24, w - 20, COLOR_CARD_BG);
    _spr.setTextFont(2);
    _spr.setTextDatum(BR_DATUM);
    _spr.setTextColor(COLOR_TEXT_MUTED, COLOR_BG);
    _spr.drawString("Toggle : ", w - 26, h - 4);
    _spr.setTextColor(COLOR_ACCENT, COLOR_BG);
    _spr.drawString("B1", w - 8, h - 4);

    if (!cgm.isValid) {
        _spr.setTextColor(COLOR_YELLOW, COLOR_BG);
        _spr.setTextDatum(MC_DATUM);
        _spr.setFreeFont(&FreeSansBold9pt7b);
        _spr.drawString(cgm.statusMessage, w / 2, h / 2 - 10);
        _spr.setTextColor(COLOR_TEXT_MUTED, COLOR_BG);
        _spr.setFreeFont(&FreeSans9pt7b);
        _spr.drawString("Fetching Dexcom...", w / 2, h / 2 + 15);
        _spr.setTextFont(1);
        return;
    }

    // Determine glucose range color using configured targets
    AppConfig& cfg = ConfigManager::getInstance().getConfig();
    int tgtLow = cfg.targetLow > 0 ? cfg.targetLow : 70;
    int tgtHigh = cfg.targetHigh > 0 ? cfg.targetHigh : 180;

    uint16_t valColor = COLOR_GREEN;
    if (cgm.value < tgtLow || cgm.value > 250) valColor = COLOR_RED;
    else if (cgm.value > tgtHigh) valColor = COLOR_YELLOW;

    String trendSym = "--";
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
    String ageStr = (cgm.ageMinutes <= 1) ? "Just now" : (String(cgm.ageMinutes) + "m ago");

    // -----------------------------------------------------------
    // View 1: Value + Graph (Horizontal / Landscape split view)
    // -----------------------------------------------------------
    if (_cgmViewMode == CgmViewMode::ValueAndGraph) {
        // Left side: Big Glucose & metrics (width 125)
        _spr.setTextColor(valColor, COLOR_BG);
        _spr.setTextDatum(TC_DATUM);
        _spr.drawString(String(cgm.value), 62, 28, 7);

        // Delta & Vector Arrow
        _spr.setTextColor(COLOR_TEXT_WHITE, COLOR_BG);
        _spr.setFreeFont(&FreeSansBold9pt7b);
        _spr.setTextDatum(MR_DATUM);
        _spr.drawString(deltaStr, 58, 92);
        drawTrendArrow(74, 92, cgm.trend, valColor, 10);

        _spr.setTextColor(COLOR_TEXT_MUTED, COLOR_BG);
        _spr.setTextFont(2);
        _spr.setTextDatum(TC_DATUM);
        _spr.drawString(ageStr, 62, 116);

        // Right side: Wide Trend Graph (width 180)
        int gx = 130;
        int gy = 26;
        int gw = w - gx - 10;
        int gh = h - gy - 32;

        _spr.fillRoundRect(gx, gy, gw, gh, 6, COLOR_CARD_BG);
        _spr.drawRoundRect(gx, gy, gw, gh, 6, 0x39E7);

        auto mapY = [&](int val) {
            int cl = constrain(val, 40, 300);
            return gy + gh - (int)((cl - 40) * (float)gh / 260.0f);
        };
        _spr.drawFastHLine(gx + 2, mapY(tgtLow), gw - 4, 0x8000);
        _spr.drawFastHLine(gx + 2, mapY(tgtHigh), gw - 4, 0x8400);

        if (history.size() > 1) {
            float stepX = (float)(gw - 16) / (float)(history.size() - 1);
            for (size_t i = 0; i < history.size(); i++) {
                int px = gx + 8 + (int)(i * stepX);
                int py = mapY(history[i]);
                uint16_t ptColor = (history[i] < tgtLow || history[i] > 250) ? COLOR_RED : (history[i] > tgtHigh ? COLOR_YELLOW : COLOR_GREEN);
                _spr.fillCircle(px, py, 2, ptColor);
                if (i > 0) {
                    int prevPx = gx + 8 + (int)((i - 1) * stepX);
                    int prevPy = mapY(history[i - 1]);
                    _spr.drawLine(prevPx, prevPy, px, py, COLOR_TEXT_MUTED);
                }
            }
        }
    }
    // -----------------------------------------------------------
    // View 2: CGM Value Big Full Screen (Horizontal)
    // -----------------------------------------------------------
    else if (_cgmViewMode == CgmViewMode::ValueFullScreen) {
        // Big Glucose Reading on left
        _spr.setTextColor(valColor, COLOR_BG);
        _spr.setTextDatum(MC_DATUM);
        _spr.drawString(String(cgm.value), w / 2 - 50, h / 2 - 5, 8); // Huge font 8

        // Prominent Change with explicit +/- badge and vector trend arrow
        uint16_t deltaCol = COLOR_GREEN;
        if (cgm.delta > 0) {
            deltaCol = (cgm.value > tgtHigh) ? COLOR_YELLOW : COLOR_GREEN;
        } else if (cgm.delta < 0) {
            deltaCol = (cgm.value < tgtLow) ? COLOR_RED : COLOR_YELLOW;
        }
        String deltaFull = (cgm.delta > 0 ? "+" : "") + String(cgm.delta);

        // Right side badge for Delta & Vector Direction
        int bx = w / 2 + 25;
        int by = h / 2 - 45;
        _spr.fillRoundRect(bx, by, 115, 68, 8, COLOR_CARD_BG);
        _spr.drawRoundRect(bx, by, 115, 68, 8, 0x39E7);

        _spr.setTextDatum(MR_DATUM);
        _spr.setTextColor(deltaCol, COLOR_CARD_BG);
        _spr.drawString(deltaFull, bx + 55, by + 34, 6); // Large font 6

        drawTrendArrow(bx + 85, by + 34, cgm.trend, deltaCol, 14);

        // Bottom sub-info
        _spr.setTextDatum(BC_DATUM);
        _spr.setTextColor(COLOR_TEXT_MUTED, COLOR_BG);
        _spr.setTextFont(2);
        _spr.drawString("mg/dL  |  " + ageStr, w / 2, h - 14);
    }
    // -----------------------------------------------------------
    // View 3: CGM Graph Full Screen (Horizontal)
    // -----------------------------------------------------------
    else if (_cgmViewMode == CgmViewMode::GraphFullScreen) {
        // Subtle stat banner at top
        _spr.setTextColor(valColor, COLOR_BG);
        _spr.setTextDatum(MR_DATUM);
        _spr.setFreeFont(&FreeSansBold9pt7b);
        _spr.drawString(String(cgm.value) + " mg/dL (" + deltaStr + ")", w / 2 + 15, 6);
        drawTrendArrow(w / 2 + 30, 6, cgm.trend, valColor, 8);

        int gx = 8;
        int gy = 26;
        int gw = w - 16;
        int gh = h - gy - 30; // Max width and height in landscape

        _spr.fillRoundRect(gx, gy, gw, gh, 6, COLOR_CARD_BG);
        _spr.drawRoundRect(gx, gy, gw, gh, 6, 0x39E7);

        auto mapY = [&](int val) {
            int cl = constrain(val, 40, 300);
            return gy + gh - (int)((cl - 40) * (float)gh / 260.0f);
        };
        _spr.drawFastHLine(gx + 2, mapY(tgtLow), gw - 4, 0x8000);
        _spr.drawFastHLine(gx + 2, mapY(tgtHigh), gw - 4, 0x8400);

        _spr.setTextColor(COLOR_TEXT_MUTED, COLOR_CARD_BG);
        _spr.setTextDatum(TL_DATUM);
        _spr.drawString(String(tgtHigh), gx + 4, mapY(tgtHigh) - 9, 1);
        _spr.drawString(String(tgtLow), gx + 4, mapY(tgtLow) + 2, 1);

        if (history.size() > 1) {
            float stepX = (float)(gw - 16) / (float)(history.size() - 1);
            for (size_t i = 0; i < history.size(); i++) {
                int px = gx + 8 + (int)(i * stepX);
                int py = mapY(history[i]);
                uint16_t ptColor = (history[i] < tgtLow || history[i] > 250) ? COLOR_RED : (history[i] > tgtHigh ? COLOR_YELLOW : COLOR_GREEN);
                _spr.fillCircle(px, py, 2, ptColor);
                if (i > 0) {
                    int prevPx = gx + 8 + (int)((i - 1) * stepX);
                    int prevPy = mapY(history[i - 1]);
                    _spr.drawLine(prevPx, prevPy, px, py, COLOR_TEXT_MUTED);
                }
            }
        }
    }
    _spr.setTextFont(1);
}

void AppManager::drawSettings() {
    _spr.fillSprite(COLOR_BG);

    // Header
    _spr.setTextColor(COLOR_ACCENT, COLOR_BG);
    _spr.setTextDatum(TC_DATUM);
    _spr.setFreeFont(&FreeSansBold9pt7b);
    _spr.drawString("SETTINGS", SCREEN_WIDTH / 2, 14);

    AppConfig& cfg = ConfigManager::getInstance().getConfig();

    int cardWidth = 152;
    int cardX = 9;

    // ----------------------------------------------------
    // Item 1: Set Device (AP)
    // ----------------------------------------------------
    bool isSel1 = (_settingsItemIndex == 0);
    uint16_t borderCol1 = isSel1 ? COLOR_ACCENT : COLOR_CARD_BG;
    uint16_t bgCol1 = isSel1 ? 0x0A24 : COLOR_CARD_BG;

    _spr.fillRoundRect(cardX, 42, cardWidth, 68, 8, bgCol1);
    _spr.drawRoundRect(cardX, 42, cardWidth, 68, 8, borderCol1);
    if (isSel1) {
        _spr.drawRoundRect(cardX - 1, 41, cardWidth + 2, 70, 8, borderCol1);
    }

    _spr.setTextDatum(TL_DATUM);
    _spr.setTextColor(isSel1 ? COLOR_TEXT_WHITE : 0xC618, bgCol1);
    _spr.setFreeFont(&FreeSansBold9pt7b);
    _spr.drawString("Set Device (AP)", cardX + 10, 48);

    _spr.setTextColor(isSel1 ? COLOR_YELLOW : COLOR_TEXT_MUTED, bgCol1);
    _spr.setTextFont(2);
    _spr.drawString(isSel1 ? "> Press B2 to start" : "WiFi & CGM Config", cardX + 10, 72);
    _spr.drawString(isSel1 ? "  captive portal" : "via captive portal", cardX + 10, 88);

    // ----------------------------------------------------
    // Item 2: Information
    // ----------------------------------------------------
    bool isSel2 = (_settingsItemIndex == 1);
    uint16_t borderCol2 = isSel2 ? COLOR_ACCENT : COLOR_CARD_BG;
    uint16_t bgCol2 = isSel2 ? 0x0A24 : COLOR_CARD_BG;

    _spr.fillRoundRect(cardX, 118, cardWidth, 158, 8, bgCol2);
    _spr.drawRoundRect(cardX, 118, cardWidth, 158, 8, borderCol2);
    if (isSel2) {
        _spr.drawRoundRect(cardX - 1, 117, cardWidth + 2, 160, 8, borderCol2);
    }

    _spr.setTextDatum(TL_DATUM);
    _spr.setTextColor(isSel2 ? COLOR_TEXT_WHITE : 0xC618, bgCol2);
    _spr.setFreeFont(&FreeSansBold9pt7b);
    _spr.drawString("Information", cardX + 10, 124);

    // System details inside Information card (Font 2 for values, Font 1 for headers)
    _spr.setTextFont(1);
    _spr.setTextColor(COLOR_TEXT_MUTED, bgCol2);
    _spr.drawString("Web Dashboard:", cardX + 10, 146);
    _spr.setTextFont(2);
    _spr.setTextColor(COLOR_GREEN, bgCol2);
    _spr.drawString(WiFi.localIP().toString(), cardX + 10, 158);

    _spr.setTextFont(1);
    _spr.setTextColor(COLOR_TEXT_MUTED, bgCol2);
    _spr.drawString("Dexcom Server:", cardX + 10, 178);
    _spr.setTextFont(2);
    _spr.setTextColor(COLOR_TEXT_WHITE, bgCol2);
    _spr.drawString(cfg.dexcomServer, cardX + 10, 190);

    _spr.setTextFont(1);
    _spr.setTextColor(COLOR_TEXT_MUTED, bgCol2);
    _spr.drawString("Account:", cardX + 10, 210);
    _spr.setTextFont(2);
    _spr.setTextColor(COLOR_TEXT_WHITE, bgCol2);
    String userDisplay = cfg.dexcomUser.length() > 0 ? cfg.dexcomUser.substring(0, 15) : "None";
    _spr.drawString(userDisplay, cardX + 10, 222);

    _spr.setTextFont(1);
    _spr.setTextColor(COLOR_TEXT_MUTED, bgCol2);
    _spr.drawString("Timezone / WiFi RSSI:", cardX + 10, 242);
    _spr.setTextFont(2);
    _spr.setTextColor(COLOR_ACCENT, bgCol2);
    _spr.drawString("GMT+" + String(cfg.timezoneOffset) + " | " + String(WiFi.RSSI()) + "dBm", cardX + 10, 254);

    // ----------------------------------------------------
    // Bottom Navigation Bar - Aligned with bottom hardware buttons
    // ----------------------------------------------------
    _spr.drawFastHLine(10, SCREEN_HEIGHT - 32, SCREEN_WIDTH - 20, COLOR_CARD_BG);
    _spr.setTextFont(2);

    // Left: B1 -> Next item
    _spr.setTextDatum(BL_DATUM);
    _spr.setTextColor(COLOR_ACCENT, COLOR_BG);
    _spr.drawString("B1", 10, SCREEN_HEIGHT - 8);
    _spr.setTextColor(COLOR_TEXT_MUTED, COLOR_BG);
    _spr.drawString(": Next", 28, SCREEN_HEIGHT - 8);

    // Right: B2 -> Select / Run
    _spr.setTextDatum(BR_DATUM);
    _spr.setTextColor(COLOR_TEXT_MUTED, COLOR_BG);
    _spr.drawString(isSel1 ? "Run AP : " : "Info : ", SCREEN_WIDTH - 28, SCREEN_HEIGHT - 8);
    _spr.setTextColor(COLOR_GREEN, COLOR_BG);
    _spr.drawString("B2", SCREEN_WIDTH - 10, SCREEN_HEIGHT - 8);

    _spr.setTextFont(1);
    _spr.pushSprite(0, 0);
}
