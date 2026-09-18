#include "dexcom_client.h"
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>

static CgmTrend parseTrend(const String& str) {
    if (str == "DoubleUp") return CgmTrend::DoubleUp;
    if (str == "SingleUp") return CgmTrend::SingleUp;
    if (str == "FortyFiveUp") return CgmTrend::FortyFiveUp;
    if (str == "Flat") return CgmTrend::Flat;
    if (str == "FortyFiveDown") return CgmTrend::FortyFiveDown;
    if (str == "SingleDown") return CgmTrend::SingleDown;
    if (str == "DoubleDown") return CgmTrend::DoubleDown;
    if (str == "NotComputable") return CgmTrend::NotComputable;
    if (str == "RateOutOfRange") return CgmTrend::RateOutOfRange;
    return CgmTrend::None;
}

bool DexcomClient::authenticate() {
    if (_username.length() == 0 || _password.length() == 0) {
        _latestReading.statusMessage = "Missing credentials";
        return false;
    }

    WiFiClientSecure client;
    client.setInsecure(); // Skip certificate validation for Dexcom API
    HTTPClient http;
    http.setTimeout(10000);

    String url = "https://" + _serverHost + "/ShareWebServices/Services/General/AuthenticatePublisherAccount";
    if (!http.begin(client, url)) {
        _latestReading.statusMessage = "HTTP connect error";
        return false;
    }

    http.addHeader("Content-Type", "application/json");
    http.addHeader("Accept", "application/json");
    http.addHeader("User-Agent", "Dexcom-Reader");

    String body = "{\"accountName\":\"" + _username + "\",\"password\":\"" + _password +
                  "\",\"applicationId\":\"" + String(APPLICATION_ID) + "\"}";

    int httpCode = http.POST(body);
    String payload = http.getString();
    http.end();

    if (httpCode == HTTP_CODE_OK && payload.length() > 0) {
        if (payload.startsWith("\"") && payload.endsWith("\"")) {
            _accountId = payload.substring(1, payload.length() - 1);
        } else {
            _accountId = payload;
        }
        return true;
    }

    _latestReading.statusMessage = "Auth failed (" + String(httpCode) + ")";
    return false;
}

bool DexcomClient::getSession() {
    if (_accountId.length() == 0) {
        if (!authenticate()) return false;
    }

    WiFiClientSecure client;
    client.setInsecure();
    HTTPClient http;
    http.setTimeout(10000);

    String url = "https://" + _serverHost + "/ShareWebServices/Services/General/LoginPublisherAccountById";
    if (!http.begin(client, url)) return false;

    http.addHeader("Content-Type", "application/json");
    http.addHeader("Accept", "application/json");
    http.addHeader("User-Agent", "Dexcom-Reader");

    String body = "{\"accountId\":\"" + _accountId + "\",\"password\":\"" + _password +
                  "\",\"applicationId\":\"" + String(APPLICATION_ID) + "\"}";

    int httpCode = http.POST(body);
    String payload = http.getString();
    http.end();

    if (httpCode == HTTP_CODE_OK && payload.length() > 0) {
        if (payload.startsWith("\"") && payload.endsWith("\"")) {
            _sessionId = payload.substring(1, payload.length() - 1);
        } else {
            _sessionId = payload;
        }
        return true;
    }

    // If session failed, clear accountId so it can re-authenticate next time
    _accountId = "";
    _latestReading.statusMessage = "Session failed (" + String(httpCode) + ")";
    return false;
}

bool DexcomClient::fetchLatestReading(CgmReading& outReading, std::vector<int>& outHistory) {
    if (_sessionId.length() == 0) {
        if (!getSession()) {
            outReading = _latestReading;
            return false;
        }
    }

    WiFiClientSecure client;
    client.setInsecure();
    HTTPClient http;
    http.setTimeout(10000);

    // Request last 24 readings (approx 2 hours of data at 5 min intervals)
    String url = "https://" + _serverHost +
                 "/ShareWebServices/Services/Publisher/ReadPublisherLatestGlucoseValues?sessionId=" +
                 _sessionId + "&minutes=1440&maxCount=24";

    if (!http.begin(client, url)) {
        _latestReading.statusMessage = "Fetch URL error";
        outReading = _latestReading;
        return false;
    }

    http.addHeader("Content-Type", "application/json");
    http.addHeader("Accept", "application/json");
    http.addHeader("User-Agent", "Dexcom-Reader");

    int httpCode = http.POST("");
    String payload = http.getString();
    http.end();

    // If 500 error or session expired, clear sessionId for renewal
    if (httpCode != HTTP_CODE_OK) {
        _sessionId = "";
        _latestReading.statusMessage = "API err " + String(httpCode);
        outReading = _latestReading;
        return false;
    }

    JsonDocument doc;
    DeserializationError error = deserializeJson(doc, payload);
    if (error) {
        _latestReading.statusMessage = "JSON error";
        outReading = _latestReading;
        return false;
    }

    JsonArray array = doc.as<JsonArray>();
    if (array.size() == 0) {
        _latestReading.statusMessage = "No readings";
        outReading = _latestReading;
        return false;
    }

    JsonObject latest = array[0];
    _latestReading.value = latest["Value"].as<int>();
    _latestReading.trend = parseTrend(latest["Trend"] | "");

    // Calculate delta from previous reading
    if (array.size() >= 2) {
        _latestReading.delta = _latestReading.value - array[1]["Value"].as<int>();
    } else {
        _latestReading.delta = 0;
    }

    // Parse ST timestamp: "/Date(1703182152000)/"
    String st = latest["ST"].as<String>();
    int openParen = st.indexOf('(');
    int closeParen = st.indexOf(')');
    if (openParen != -1 && closeParen != -1) {
        String msStr = st.substring(openParen + 1, closeParen);
        // Epoch in seconds
        _latestReading.timestamp = (time_t)(msStr.substring(0, msStr.length() - 3).toInt());
    }

    time_t nowTime = time(nullptr);
    if (nowTime > 1000000000 && _latestReading.timestamp > 0) {
        _latestReading.ageMinutes = (nowTime - _latestReading.timestamp) / 60;
        if (_latestReading.ageMinutes < 0) _latestReading.ageMinutes = 0;
    } else {
        _latestReading.ageMinutes = 0;
    }

    _latestReading.isValid = true;
    _latestReading.statusMessage = "OK";

    // Populate history (oldest to newest)
    _history.clear();
    for (int i = (int)array.size() - 1; i >= 0; i--) {
        _history.push_back(array[i]["Value"].as<int>());
    }

    outReading = _latestReading;
    outHistory = _history;
    return true;
}
