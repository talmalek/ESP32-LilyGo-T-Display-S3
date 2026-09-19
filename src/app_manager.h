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

class AppManager {
public:
    static AppManager& getInstance() {
        static AppManager instance;
        return instance;
    }

    void begin();
    void update();

    // Button event handlers
    void onBtn1Click();       // Cycle next app in launcher
    void onBtn2Click();       // Select/Enter app
    void onChordPress();      // Both buttons pressed together -> Exit to Launcher

    void switchTo(AppState newState);
    void launchAPPortal();
    void factoryReset();
    void setBrightness(int val);
    void triggerImmediateCgmFetch();
    AppState getCurrentState() const { return _currentState; }
    String getCurrentStateName() const;

private:
    AppManager() = default;

    void initDisplay();
    void initButtons();
    void initWiFi();

    // Render methods for 170 x 320
    void drawLauncher();
    void drawClock();
    void drawCGM();
    void drawSettings();
    void drawWiFiPortalScreen();

    // Background tasks
    void updateTime();
    void updateCGM();

    TFT_eSPI _tft = TFT_eSPI();
    TFT_eSprite _spr = TFT_eSprite(&_tft); // 170x320 Double buffer sprite

    AppState _currentState = AppState::Launcher;
    int _selectedAppIndex = 0; // 0 = Clock, 1 = CGM, 2 = Settings
    const int APP_COUNT = 3;

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
