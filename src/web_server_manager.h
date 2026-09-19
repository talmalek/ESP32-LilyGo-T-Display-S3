#pragma once
#include <Arduino.h>
#include <WebServer.h>

class WebServerManager {
public:
    static WebServerManager& getInstance() {
        static WebServerManager instance;
        return instance;
    }

    void begin();
    void handleClient();
    bool isRunning() const { return _running; }

private:
    WebServerManager() : _server(80) {}

    void setupRoutes();
    void handleRoot();
    void handleGetStatus();
    void handleSaveSettings();
    void handleControl();

    WebServer _server;
    bool _running = false;
};
