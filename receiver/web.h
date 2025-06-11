// jangjunwon2/comm/comm-6295635354ffa5ad160f5b3be2c0db2652b69d97/main/web.h
// =========================================================================
// web.h
// =========================================================================

/**
 * @file web.h
 * @brief WebManager 클래스의 헤더 파일입니다.
 * @version 7.0.0
 * @date 2024-06-13
 */
#pragma once
#ifndef WEB_H
#define WEB_H

#include <ESPAsyncWebServer.h> //
#include <ArduinoJson.h> //
#include "config.h" //
#include "utils.h" //

class ModeManager;

class WebManager {
public:
    WebManager();
    void begin(ModeManager* modeMgr);
    void startServer();
    void stopServer();
    bool isServerRunning() const;
    void performUpdate();
    void broadcastTestComplete();
    void broadcastWifiStatus(); // [NEW] Added for WiFi status broadcast on home page

private:
    AsyncWebServer _server;
    AsyncWebSocket _ws;
    ModeManager* _modeManager;
    bool _isServerRunning;
    bool _otaUpdateDownloaded;

    String _currentFirmwareVersion;
    String _latestOtaVersion;
    String _otaChangeLog;
    bool _otaUpdateAvailable;
    
    SemaphoreHandle_t _otaDataMutex;
    
    static void otaCheckVersionTask(void *pvParameters);
    static void otaDownloadTask(void *pvParameters);
    static void wifiScanTask(void *pvParameters);
    
    void onWsEvent(AsyncWebSocket *server, AsyncWebSocketClient *client, AwsEventType type, void *arg, uint8_t *data, size_t len);

    void setupRoutes();
    void handleRoot(AsyncWebServerRequest *request);
    void handleWifiConfigPage(AsyncWebServerRequest *request);
    void handleFirmwareUpdatePage(AsyncWebServerRequest *request);
    void handleTestModePage(AsyncWebServerRequest *request);
    void handleExit(AsyncWebServerRequest *request);
    void handleNotFound(AsyncWebServerRequest *request);
    
    void handleScanWifiApi(AsyncWebServerRequest *request);
    void handleConnectWifiApi(AsyncWebServerRequest *request);
    void handleWifiStatusApi(AsyncWebServerRequest *request);
    void handleCheckOtaApi(AsyncWebServerRequest *request);
    void handleDownloadOtaApi(AsyncWebServerRequest *request);
    void handleDeviceStatusApi(AsyncWebServerRequest *request);
    void handleSetDeviceIdApi(AsyncWebServerRequest *request);
    void handleSetTestParamsApi(AsyncWebServerRequest *request);
    void handleRunTestApi(AsyncWebServerRequest *request);
    
    void startSoftAP();
    bool fetchOtaVersionInfo();
    void downloadAndApplyOta();
    String getPageHeader(const String& title);
    String getPageFooter(bool showHomeButton);
    void setupLogBroadcaster();
    void broadcastStatusUpdate();
    void broadcastOtaProgress(int progress); // [NEW] For OTA progress
};

#endif // WEB_H