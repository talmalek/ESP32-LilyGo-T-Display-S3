#pragma once
#include <Arduino.h>
#include <vector>

enum class CgmTrend {
    None,
    DoubleUp,
    SingleUp,
    FortyFiveUp,
    Flat,
    FortyFiveDown,
    SingleDown,
    DoubleDown,
    NotComputable,
    RateOutOfRange
};

struct CgmReading {
    int value = 0;              // mg/dL
    int delta = 0;              // diff compared to previous reading
    CgmTrend trend = CgmTrend::None;
    time_t timestamp = 0;       // Epoch time in seconds
    int ageMinutes = 0;         // Minutes since reading was taken
    bool isValid = false;
    String statusMessage = "Waiting...";
};

class DexcomClient {
public:
    static DexcomClient& getInstance() {
        static DexcomClient instance;
        return instance;
    }

    void setCredentials(const String& user, const String& pass, const String& server) {
        _username = user;
        _password = pass;
        _server = server;
        _serverHost = (_server == "US") ? "share1.dexcom.com" : "shareous1.dexcom.com";
        _accountId = "";
        _sessionId = "";
    }

    bool fetchLatestReading(CgmReading& outReading, std::vector<int>& outHistory);

    const CgmReading& getCachedReading() const {
        return _latestReading;
    }

    const std::vector<int>& getHistory() const {
        return _history;
    }

private:
    DexcomClient() = default;

    bool authenticate();
    bool getSession();

    String _username;
    String _password;
    String _server;
    String _serverHost;
    String _accountId;
    String _sessionId;

    CgmReading _latestReading;
    std::vector<int> _history;

    const char* APPLICATION_ID = "d89443d2-327c-4a6f-89e5-496bbb0317db";
};
