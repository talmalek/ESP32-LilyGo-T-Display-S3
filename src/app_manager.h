#pragma once
#include <Arduino.h>
#include <TFT_eSPI.h>
#include <OneButton.h>
#include <WiFi.h>
#include <WiFiManager.h>
#include "hardware_config.h"
#include "config_manager.h"
#include "dexcom_client.h"

enum class AppState {
    Launcher,
    Clock,
    CGM,
    Settings
};

enum class CgmViewMode {
    ValueAndGraph = 0,
    ValueFullScreen = 1,
    GraphFullScreen = 2
};

enum class ClockFaceMode {
    Modern = 0,     // Digital time with large seconds filling bottom, date & stats
    BoldDigital = 1, // Massive stacked Hour & Minute numbers, full seconds bar
    ClassicClean = 2 // Minimalist aesthetic with orbital second indicator & clean layout
};

class AppManager {
public:
    static AppManager& getInstance() {
        static AppManager instance;
        return instance;
    }

    void begin();
    void update();

    // Button event handlers
    void onBtn1Click();       // Cycle next app / Toggle CGM view / Cycle Clock face
    void onBtn2Click();       // Select app / Rotate CGM orientation / Rotate Clock orientation
    void onChordPress();      // Both buttons pressed together -> Exit to Launcher

    void switchTo(AppState newState);
    void launchAPPortal();
    void factoryReset();
    void setBrightness(int val);
    void triggerImmediateCgmFetch();
    AppState getCurrentState() const { return _currentState; }
    String getCurrentStateName() const;

    // View control methods
    void toggleCgmView();
    void setCgmView(CgmViewMode mode);
    void toggleCgmOrientation();
    void toggleClockFace();
    void setClockFace(ClockFaceMode mode);
    void toggleClockOrientation();
    CgmViewMode getCgmViewMode() const { return _cgmViewMode; }
    ClockFaceMode getClockFaceMode() const { return _clockFaceMode; }
    bool isCgmHorizontal() const { return _cgmHorizontal; }
    bool isClockHorizontal() const { return _clockHorizontal; }

private:
    AppManager() = default;

    void initDisplay();
    void initButtons();
    void initWiFi();
    void setScreenOrientation(bool horizontal);

    // Render methods
    void drawLauncher();
    void drawClock();
    void drawClockVertical(int w, int h, const struct tm& timeinfo);
    void drawClockHorizontal(int w, int h, const struct tm& timeinfo);
    void drawCGM();
    void drawCGMVertical(int w, int h, const CgmReading& cgm, const std::vector<int>& history, int syncSecRemaining);
    void drawCGMHorizontal(int w, int h, const CgmReading& cgm, const std::vector<int>& history, int syncSecRemaining);
    void drawSettings();
    void drawWiFiPortalScreen();
    void drawTrendArrow(int cx, int cy, CgmTrend trend, uint16_t color, int size = 10);

    // Background tasks
    void updateTime();
    void updateCGM();

    TFT_eSPI _tft = TFT_eSPI();
    TFT_eSprite _spr = TFT_eSprite(&_tft); // Double buffer sprite

    AppState _currentState = AppState::Launcher;
    int _selectedAppIndex = 0; // 0 = Clock, 1 = CGM, 2 = Settings
    const int APP_COUNT = 3;
    int _settingsItemIndex = 0; // 0 = Set Device (AP), 1 = Information

    // Clock App Customization States
    ClockFaceMode _clockFaceMode = ClockFaceMode::Modern;
    bool _clockHorizontal = false;

    // CGM App Customization States
    CgmViewMode _cgmViewMode = CgmViewMode::ValueAndGraph;
    bool _cgmHorizontal = false;

    // Direct button states (debounced)
    bool _b1Prev = true;
    bool _b2Prev = true;
    unsigned long _b1DownTime = 0;
    unsigned long _b2DownTime = 0;
    bool _b1Handled = false;
    bool _b2Handled = false;
    bool _chordHandled = false;

    // Time tracking
    unsigned long _lastClockDraw = 0;
    unsigned long _lastCgmDraw = 0;
    TaskHandle_t _cgmTaskHandle = nullptr;
    volatile bool _cgmFetchInProgress = false;
};
