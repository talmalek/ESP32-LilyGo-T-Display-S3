#pragma once
#include <Arduino.h>
#include <Preferences.h>

struct AppConfig {
    String dexcomUser = "";
    String dexcomPass = "";
    String dexcomServer = "NON-US"; // "US" or "NON-US"
    int timezoneOffset = 2;         // Hours from UTC (e.g. 2 for Israel Standard, 3 for Daylight)
    int screenBrightness = 200;     // 10 - 255
    bool use24HourFormat = true;
    int targetLow = 70;             // mg/dL low threshold
    int targetHigh = 180;           // mg/dL high threshold
};

class ConfigManager {
public:
    static ConfigManager& getInstance() {
        static ConfigManager instance;
        return instance;
    }

    void begin() {
        _prefs.begin("cgm_clock_app", false);
        _config.dexcomUser = _prefs.getString("dex_user", "");
        _config.dexcomPass = _prefs.getString("dex_pass", "");
        _config.dexcomServer = _prefs.getString("dex_server", "NON-US");
        _config.timezoneOffset = _prefs.getInt("tz_offset", 2);
        _config.screenBrightness = _prefs.getInt("brightness", 200);
        _config.use24HourFormat = _prefs.getBool("time_24h", true);
        _config.targetLow = _prefs.getInt("tgt_low", 70);
        _config.targetHigh = _prefs.getInt("tgt_high", 180);
    }

    AppConfig& getConfig() {
        return _config;
    }

    void save() {
        _prefs.putString("dex_user", _config.dexcomUser);
        _prefs.putString("dex_pass", _config.dexcomPass);
        _prefs.putString("dex_server", _config.dexcomServer);
        _prefs.putInt("tz_offset", _config.timezoneOffset);
        _prefs.putInt("brightness", _config.screenBrightness);
        _prefs.putBool("time_24h", _config.use24HourFormat);
        _prefs.putInt("tgt_low", _config.targetLow);
        _prefs.putInt("tgt_high", _config.targetHigh);
    }

    void resetAll() {
        _prefs.clear();
    }

private:
    ConfigManager() = default;
    Preferences _prefs;
    AppConfig _config;
};
