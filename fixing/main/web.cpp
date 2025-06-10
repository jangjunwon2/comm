// =========================================================================
// web.cpp
// =========================================================================

/**
 * @file web.cpp
 * @brief WebManager 클래스의 구현입니다.
 * @version 8.6.0
 * @date 2024-06-14
 */
#include "web.h"
#include "mode.h"
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <Update.h>
#include <vector>
#include <algorithm>
#include <esp_task_wdt.h>

static volatile bool isScanning = false;

WebManager::WebManager() :
    _server(80), _ws("/ws"), _modeManager(nullptr), _isServerRunning(false),
    _otaUpdateDownloaded(false), _currentFirmwareVersion(FIRMWARE_VERSION),
    _latestOtaVersion("N/A"), _otaChangeLog("N/A"), _otaUpdateAvailable(false) {
    _otaDataMutex = xSemaphoreCreateMutex();
}

void WebManager::begin(ModeManager* modeMgr) {
    _modeManager = modeMgr;
    setupRoutes();
    setupLogBroadcaster();
    Log::Info(PSTR("WEB: WebManager initialized."));
}

void WebManager::startServer() {
    if (_isServerRunning) return;
    Log::Info(PSTR("WEB: Starting web server..."));
    WiFi.mode(WIFI_AP_STA);
    startSoftAP();
    String storedSsid = NVS::loadWifiSsid();
    if (!storedSsid.isEmpty()) {
        WiFi.begin(storedSsid.c_str(), NVS::loadWifiPassword().c_str());
    }
    _server.begin();
    _isServerRunning = true;
    _otaUpdateDownloaded = false;
    if (_modeManager) _modeManager->setUpdateDownloaded(false);
    Log::Info(PSTR("WEB: Server started. AP IP: http://%s"), WiFi.softAPIP().toString().c_str());
}

void WebManager::stopServer() {
    if (!_isServerRunning) return;
    Log::Info(PSTR("WEB: Stopping web server..."));
    _ws.closeAll();
    _server.end();
    WiFi.softAPdisconnect(true);
    WiFi.disconnect(true, true);
    WiFi.mode(WIFI_OFF);
    _isServerRunning = false;
    Log::Info(PSTR("WEB: Server stopped and WiFi turned off."));
}

bool WebManager::isServerRunning() const { return _isServerRunning; }
void WebManager::performUpdate() { if (_otaUpdateDownloaded) ESP.restart(); }
void WebManager::broadcastTestComplete() { 
    _ws.textAll("{\"type\":\"test_completed\"}"); 
}
void WebManager::broadcastOtaProgress(int progress) {
    StaticJsonDocument<128> doc;
    doc["type"] = "ota_progress";
    doc["progress"] = progress;
    String output;
    serializeJson(doc, output);
    _ws.textAll(output);
}

void WebManager::broadcastWifiStatus() {
    DynamicJsonDocument doc(JSON_DOC_SIZE_API_RESP);
    doc["type"] = "wifi_status_update";
    doc["connected"] = (WiFi.status() == WL_CONNECTED);
    if (doc["connected"]) {
        doc["ssid"] = WiFi.SSID();
        doc["ip"] = WiFi.localIP().toString();
    } else {
        doc["ssid"] = "N/A";
        doc["ip"] = "0.0.0.0";
    }
    String output;
    serializeJson(doc, output);
    _ws.textAll(output);
}

void WebManager::setupRoutes() {
    _server.on("/", HTTP_GET, [this](AsyncWebServerRequest* r){ handleRoot(r); });
    _server.on("/wifi", HTTP_GET, [this](AsyncWebServerRequest* r){ handleWifiConfigPage(r); });
    _server.on("/update", HTTP_GET, [this](AsyncWebServerRequest* r){ handleFirmwareUpdatePage(r); });
    _server.on("/test", HTTP_GET, [this](AsyncWebServerRequest* r){ handleTestModePage(r); });
    _server.on("/exit", HTTP_GET, [this](AsyncWebServerRequest* r){ handleExit(r); });
    _server.on("/api/scanwifi", HTTP_GET, [this](AsyncWebServerRequest* r){ handleScanWifiApi(r); });
    _server.on("/api/connectwifi", HTTP_POST, [this](AsyncWebServerRequest* r){ handleConnectWifiApi(r); });
    _server.on("/api/wifistatus", HTTP_GET, [this](AsyncWebServerRequest* r){ handleWifiStatusApi(r); });
    _server.on("/api/checkota", HTTP_GET, [this](AsyncWebServerRequest* r){ 
        xTaskCreate(otaCheckVersionTask, "otaCheckTask", 8192, this, 5, NULL);
        r->send(200, "application/json", "{\"status\":\"checking\"}");
    });
    _server.on("/api/downloadota", HTTP_POST, [this](AsyncWebServerRequest* r){ 
        xTaskCreate(otaDownloadTask, "otaDownloadTask", 10240, this, 2, NULL);
        r->send(200, "application/json", "{\"status\":\"download_started\"}");
    });
    _server.on("/api/devicestatus", HTTP_GET, [this](AsyncWebServerRequest* r){ handleDeviceStatusApi(r); });
    _server.on("/api/setdeviceid", HTTP_POST, [this](AsyncWebServerRequest* r){ handleSetDeviceIdApi(r); });
    _server.on("/api/runtest", HTTP_POST, [this](AsyncWebServerRequest* r){ handleRunTestApi(r); });
    _server.onNotFound([this](AsyncWebServerRequest* r){ handleNotFound(r); });
    _ws.onEvent([this](auto *s, auto *c, AwsEventType t, void *a, uint8_t *d, size_t l) { onWsEvent(s, c, t, a, d, l); });
    _server.addHandler(&_ws);
}

void WebManager::onWsEvent(AsyncWebSocket *server, AsyncWebSocketClient *client, AwsEventType type, void *arg, uint8_t *data, size_t len) {
    if (_modeManager) _modeManager->recordWebApiActivity();
    if (type == WS_EVT_CONNECT) {
        Log::Debug(PSTR("WEB: WebSocket client #%u connected."), client->id());
        broadcastStatusUpdate(); 
        broadcastWifiStatus(); 
    } else if (type == WS_EVT_DISCONNECT) {
        Log::Debug(PSTR("WEB: WebSocket client #%u disconnected."), client->id());
    }
}

// --- Page Handlers ---

void WebManager::handleRoot(AsyncWebServerRequest* request) {
    _modeManager->recordWebApiActivity();
    AsyncResponseStream *response = request->beginResponseStream("text/html; charset=UTF-8");
    response->addHeader("Cache-Control", "no-store, no-cache, must-revalidate, max-age=0");
    response->addHeader("Pragma", "no-cache");

    response->print(getPageHeader("Mystic Lab Device"));
    response->print("<div class='card'><h3>Wi-Fi Status</h3><p id='home-wifi-status'>Loading...</p></div>"); 
    response->print("<div class='card'><h3>Device Control</h3>"
            "<p><a href='/wifi' class='btn'>Wi-Fi Settings</a></p>"
            "<p><a href='/update' class='btn'>Firmware Update</a></p>"
            "<p><a href='/test' class='btn'>Test Mode</a></p>"
            "<p><a href='/exit' class='btn btn-danger'>Exit Wi-Fi Mode</a></p>"
            "</div>");
    response->print(getPageFooter(false));
    response->print(R"rawliteral(
        <script>
            let ws;
            function connectWsForStatus(){
                ws = new WebSocket("ws://"+window.location.host+"/ws");
                ws.onopen = () => { console.log("WebSocket for status connected!"); };
                ws.onmessage = e => {
                    try { 
                        const d = JSON.parse(e.data); 
                        if (d.type === "wifi_status_update") { 
                            let s = document.getElementById("home-wifi-status");
                            if (d.connected) {
                                s.innerHTML = "Connected to <b>" + d.ssid + "</b><br>IP Address: " + d.ip;
                            } else {
                                s.textContent = "Not connected. AP Mode is active.";
                            }
                        } 
                    } catch(err) { console.error("WS message error:", err); }
                };
                ws.onclose = () => { console.log("WebSocket for status disconnected. Reconnecting..."); setTimeout(connectWsForStatus, 2000); };
            }
            window.onload = connectWsForStatus;
        </script>
    )rawliteral"); 
    request->send(response);
}

void WebManager::handleWifiConfigPage(AsyncWebServerRequest* request) {
    _modeManager->recordWebApiActivity();
    AsyncResponseStream *response = request->beginResponseStream("text/html; charset=UTF-8");
    response->addHeader("Cache-Control", "no-store, no-cache, must-revalidate, max-age=0");
    response->addHeader("Pragma", "no-cache");
    
    response->print(getPageHeader("Wi-Fi Settings"));
    response->print(R"rawliteral(
        <div class='card'>
            <h3>Current Status</h3>
            <p id='wifi-status'>Loading...</p>
        </div>
        <div class='card'>
            <h3>Connect to a Network</h3>
            <form id='connect-form' onsubmit='return connectToWifi(event)'>
                <label for='ssid-select'>Select Network:</label>
                <select id='ssid-select' name='ssid'></select>
                <label for='password'>Password:</label>
                <input type='password' id='password' name='password' placeholder='Enter password (if any)'>
                <div style='text-align:right; margin-bottom:15px;'>
                   <button type='button' onclick='scanWifi()' id='scan-btn' class='btn' style='font-size:12px; padding: 5px 10px; min-width:auto;'>Scan Again</button>
                </div>
                <p><input type='submit' id='connect-btn' value='Save & Connect' class='btn'></p>
            </form>
            <div id='connect-result' style='margin-top:10px; font-weight:bold;'></div>
        </div>
        <script>
            let ws;
            function connectWs(){
                ws = new WebSocket("ws://"+window.location.host+"/ws");
                ws.onopen = () => { console.log("WebSocket connected!"); fetchStatus(); scanWifi(); };
                ws.onmessage = e => {
                    try { 
                        const d = JSON.parse(e.data); 
                        if (d.type === "wifi_scan_result") { 
                            updateWifiList(d.networks); 
                        } else if (d.type === "wifi_status_update") {
                            updateWifiStatusDisplay(d);
                        }
                    } catch(err) { console.error("WS message error:", err); }
                };
                ws.onclose = () => { console.log("WebSocket disconnected. Reconnecting..."); setTimeout(connectWs, 2000); };
            }
            function scanWifi() {
                let s=document.getElementById("ssid-select"), b=document.getElementById("scan-btn"), r=document.getElementById("connect-result");
                s.innerHTML = "<option value=''>Scanning...</option>"; b.disabled = true; r.innerHTML = "";
                fetch("/api/scanwifi").finally(() => { b.disabled=false; });
            }
            function fetchStatus(){
                fetch("/api/wifistatus").then(r=>r.json()).then(d=>{ updateWifiStatusDisplay(d); });
            }
            function updateWifiStatusDisplay(d){
                let s=document.getElementById("wifi-status");
                if(d.connected){
                    s.innerHTML="Connected to <b>"+d.ssid+"</b><br>IP Address: "+d.ip;
                } else {
                    s.textContent="Not connected. AP Mode is active.";
                }
            }
            function updateWifiList(nets){
                let s=document.getElementById("ssid-select"), b=document.getElementById("scan-btn");
                s.innerHTML = "<option value=''>-- Select a Network --</option>";
                if(nets && nets.length > 0) nets.forEach(net=>{ s.innerHTML += `<option value='`+net.ssid+`'>`+net.ssid+` (`+net.rssi+` dBm)</option>`; });
                else s.innerHTML = "<option value=''>No networks found. Try scanning again.</option>";
                b.disabled = false;
            }
            function connectToWifi(e){
                e.preventDefault();
                let form = e.target;
                let r = document.getElementById("connect-result");
                let b = document.getElementById("connect-btn");
                let scanBtn = document.getElementById("scan-btn");
                r.textContent = "Attempting to connect...";
                b.disabled = true;
                scanBtn.disabled = true;

                let pollIntervalId;
                
                fetch("/api/connectwifi",{method:"POST",body:new FormData(form)})
                    .then(resp => resp.json())
                    .then(data => {
                        if (data.status !== 'connection_started') {
                            r.innerHTML = "<p style='color:red;'>Failed: " + (data.error || "Unknown error") + "</p>";
                            b.disabled = false; scanBtn.disabled = false;
                            return;
                        }

                        r.textContent = "Connecting... Please wait.";
                        let pollCount = 0;
                        const maxPolls = 10;
                        
                        pollIntervalId = setInterval(() => {
                            fetch("/api/wifistatus")
                                .then(res => res.json())
                                .then(status => {
                                    pollCount++;
                                    if (status.connected) {
                                        clearInterval(pollIntervalId);
                                        r.innerHTML = "<p style='color:green;'>Success!<br>SSID: " + status.ssid + "<br>IP: " + status.ip + "</p>";
                                        setTimeout(() => { window.location.href = '/'; }, 3000);
                                    } else if (status.status === 'failed') {
                                        clearInterval(pollIntervalId);
                                        r.innerHTML = "<p style='color:red;'>Connection Failed. Please check password.</p>";
                                        b.disabled = false; scanBtn.disabled = false;
                                    } else if (pollCount >= maxPolls) {
                                        clearInterval(pollIntervalId);
                                        r.innerHTML = "<p style='color:red;'>Connection Timed Out. Please check your password.</p>";
                                        b.disabled = false; scanBtn.disabled = false;
                                    }
                                });
                        }, pollCount < 5 ? 1000 : 2000);
                    })
                    .catch(err => {
                        r.innerHTML = "<p style='color:red;'>Error sending command.</p>";
                        b.disabled = false; scanBtn.disabled = false;
                        if(pollIntervalId) clearInterval(pollIntervalId);
                    });
                return false;
            }
            window.onload = connectWs;
        </script>
    )rawliteral");
    response->print(getPageFooter(true));
    request->send(response);
}

void WebManager::handleFirmwareUpdatePage(AsyncWebServerRequest* request) {
    _modeManager->recordWebApiActivity();
    AsyncResponseStream *response = request->beginResponseStream("text/html; charset=UTF-8");
    response->addHeader("Cache-Control", "no-store, no-cache, must-revalidate, max-age=0");
    response->addHeader("Pragma", "no-cache");

    response->print(getPageHeader("Firmware Update"));
    response->print(R"rawliteral(
        <div class='card'>
            <p>Current Version: <b id='current-v'>-</b><br>Latest on Server: <b id='latest-v'>-</b></p>
            <div id='changelog' style='text-align:left; background:#f0f0f0; padding:10px; border-radius:5px; margin-bottom:15px; white-space:pre-wrap;'></div>
            <p id='update-status'></p>
            <button id='update-btn' class='btn hidden' onclick='update()'>Download Update</button>
            <p id='download-notice' class='hidden' style='font-style:italic; color: #d9534f;'>Firmware will be downloaded now. The update will be applied when you exit Wi-Fi mode.</p>
        </div>
        <script>
            function update(){if(!confirm("Start download? The update will be applied on exit."))return;document.getElementById("update-btn").disabled=true;document.getElementById("download-notice").classList.remove("hidden");fetch("/api/downloadota",{method:"POST"});}
            function updateUI(d){
                document.getElementById("current-v").textContent=d.current_version;
                document.getElementById("latest-v").textContent=d.latest_version;
                document.getElementById("changelog").textContent = d.changelog || "Could not retrieve change log.";
                let btn = document.getElementById("update-btn");
                if(d.update_available){
                    document.getElementById("update-status").innerHTML="<b style='color:green;'>Update available!</b>";
                    btn.classList.remove("hidden");
                }else{
                    document.getElementById("update-status").textContent= d.internet_ok ? "You are on the latest version." : "Connect to the internet to check for updates.";
                    btn.classList.add("hidden");
                }
            }
            let ws=new WebSocket("ws://"+window.location.host+"/ws");
            ws.onmessage=e=>{
                try{
                    let d=JSON.parse(e.data);
                    let btn = document.getElementById("update-btn");
                    if(d.type==="ota_status") updateUI(d);
                    if(d.type==="ota_progress") btn.textContent="Downloading... ("+d.progress+"%)";
                    if(d.type==="ota_result") {
                        alert(d.msg);
                        if(d.msg.includes("OK")) { btn.textContent="Download Complete"; } 
                        else { btn.textContent="Download Update"; btn.disabled=false; }
                    }
                }catch(err){}
            };
            window.onload = () => { fetch("/api/checkota"); };
        </script>
    )rawliteral");
    response->print(getPageFooter(true));
    request->send(response);
}

void WebManager::handleTestModePage(AsyncWebServerRequest* request) {
    if (_modeManager) _modeManager->switchToMode(DeviceMode::MODE_TEST, false);
    AsyncResponseStream *response = request->beginResponseStream("text/html; charset=UTF-8");
    response->addHeader("Cache-Control", "no-store, no-cache, must-revalidate, max-age=0");
    response->addHeader("Pragma", "no-cache");

    response->print(getPageHeader("Test Mode"));
    response->print(R"rawliteral(
        <div class='card'>
            <h3>Device Settings</h3>
            <table style='width:100%; text-align:left; border-spacing: 0 10px; border-collapse: separate;'>
              <tr>
                <td style='width:140px;'><label for='dev-id'>Device ID :</label></td>
                <td>
                  <div style='display:flex; align-items:center;'>
                    <input type='number' id='dev-id' min='1' max='20' style='width: 80px; margin:0;'>
                    <button onclick='saveId()' class='btn' style='padding:5px 10px; min-width:auto; margin-left: 10px;'>Save</button>
                  </div>
                </td>
              </tr>
              <tr>
                <td><label for='delay-s'>Delay Timer (s) :</label></td>
                <td><input type='number' id='delay-s' placeholder='Delay' step='0.1' style='width: 80px;'></td>
              </tr>
              <tr>
                <td><label for='play-s'>Play Timer (s) :</label></td>
                <td><input type='number' id='play-s' placeholder='Play' step='0.1' style='width: 80px;'></td>
              </tr>
            </table>
            <p><button onclick='runTest()' id='run-test-btn' class='btn'>Run Manual Test</button></p>
        </div>
        <div class='card'>
            <h3>Live Log (<a href='javascript:void(0);' onclick='document.getElementById("log").innerHTML=""'>Clear</a>)</h3>
            <div id='log' style='height:300px;overflow-y:scroll;border:1px solid #ccc;text-align:left;padding:5px;font-family:monospace;font-size:0.9em;background:#333;color:#eee;white-space:pre-wrap;'></div>
        </div>
        <script>
            let log=document.getElementById("log");
            function getStatus(){
                fetch("/api/devicestatus")
                .then(r=>r.json())
                .then(d=>{
                    document.getElementById("dev-id").value = d.device_id;
                    document.getElementById("delay-s").value = d.test_delay_ms / 1000.0;
                    document.getElementById("play-s").value = d.test_play_ms / 1000.0;
                });
            }
            function saveId(){
                const id = document.getElementById("dev-id").value;
                fetch("/api/setdeviceid",{method:"POST",headers:{"Content-Type":"application/x-www-form-urlencoded"},body:"id="+id})
                .then(()=> { 
                    log.innerHTML+=`<div style="color:#00ff00;">ID Saved: ${id}</div>`;
                    log.scrollTop=log.scrollHeight;
                });
            }
            function runTest(){
                let btn = document.getElementById('run-test-btn');
                btn.disabled = true;
                btn.textContent = 'Running...';
                
                let delayMs = parseFloat(document.getElementById('delay-s').value) * 1000;
                let playMs = parseFloat(document.getElementById('play-s').value) * 1000;

                let formData = new URLSearchParams();
                formData.append('delay', delayMs);
                formData.append('play', playMs);

                fetch("/api/runtest",{
                    method:"POST",
                    headers: { 'Content-Type': 'application/x-www-form-urlencoded' },
                    body: formData
                });
            }
            let ws=new WebSocket("ws://"+window.location.host+"/ws");
            ws.onmessage=e=>{
                try{
                    let d=JSON.parse(e.data);
                    if(d.type==="log"){
                        if (d.level === "TEST") {
                            log.innerHTML+=`<div style="color:#fff;">${d.msg}</div>`;
                        }
                        log.scrollTop=log.scrollHeight;
                    }
                    if(d.type==="test_completed"){
                         let btn = document.getElementById('run-test-btn');
                         btn.disabled = false;
                         btn.textContent = 'Run Manual Test';
                         log.innerHTML+=`<div style="color:#fff;">Test Completed.</div>`;
                         log.scrollTop=log.scrollHeight;
                    }
                }catch(e){}
            };
            window.onload=getStatus;
        </script>
    )rawliteral");
    response->print(getPageFooter(true));
    request->send(response);
}

void WebManager::handleExit(AsyncWebServerRequest* request) {
    AsyncResponseStream *response = request->beginResponseStream("text/html; charset=UTF-8");
    response->addHeader("Cache-Control", "no-store, no-cache, must-revalidate, max-age=0");
    response->addHeader("Pragma", "no-cache");

    response->print(getPageHeader("Exiting Wi-Fi Mode"));
    response->print("<p>The device will now return to normal operation. You can close this window.</p>");
    if (_otaUpdateDownloaded) {
        response->print("<p style='color:blue;font-weight:bold;'>An update was downloaded and will be applied on reboot.</p>");
    }
    response->print(getPageFooter(false));
    request->send(response);
    delay(100);
    if (_modeManager) _modeManager->exitWifiMode();
}

void WebManager::handleNotFound(AsyncWebServerRequest* request) { request->send(404, "text/plain", "Not Found"); }

// --- API 핸들러 ---
void WebManager::handleScanWifiApi(AsyncWebServerRequest* request) {
    xTaskCreate(wifiScanTask, "wifiScanTask", 4096, this, 5, NULL);
    request->send(200, "application/json", "{\"status\":\"scan_started\"}");
}

void WebManager::handleConnectWifiApi(AsyncWebServerRequest* request) {
    String ssid, pass;
    if (request->hasParam("ssid", true)) ssid = request->getParam("ssid", true)->value();
    if (request->hasParam("password", true)) pass = request->getParam("password", true)->value();
    
    DynamicJsonDocument doc(JSON_DOC_SIZE_API_RESP);
    if (ssid.length() > 0) {
        NVS::saveWifiSsid(ssid);
        NVS::saveWifiPassword(pass);
        Log::Info(PSTR("WEB: Attempting to connect to SSID: %s"), ssid.c_str());
        WiFi.begin(ssid.c_str(), pass.c_str());
        doc["status"] = "connection_started";
    } else {
        doc["status"] = "fail";
        doc["error"] = "Missing SSID";
    }
    String output;
    serializeJson(doc, output);
    request->send(200, "application/json", output);
}

void WebManager::handleWifiStatusApi(AsyncWebServerRequest* request) {
    DynamicJsonDocument doc(JSON_DOC_SIZE_API_RESP);
    wl_status_t status = WiFi.status();
    doc["connected"] = (status == WL_CONNECTED);
    
    if (doc["connected"]) {
        doc["ssid"] = WiFi.SSID();
        doc["ip"] = WiFi.localIP().toString();
        doc["status"] = "connected";
    } else {
        doc["ssid"] = NVS::loadWifiSsid(); 
        if (status == WL_NO_SSID_AVAIL || status == WL_CONNECT_FAILED) {
            doc["status"] = "failed";
            WiFi.disconnect(false, true); 
            delay(100);
        } else {
            doc["status"] = "connecting";
        }
    }
    String output; serializeJson(doc, output);
    request->send(200, "application/json", output);
}

void WebManager::handleDeviceStatusApi(AsyncWebServerRequest* request) {
    DynamicJsonDocument doc(JSON_DOC_SIZE_API_RESP);
    doc["device_id"] = NVS::loadDeviceId();
    doc["test_delay_ms"] = NVS::loadTestDelay();
    doc["test_play_ms"] = NVS::loadTestPlay();
    String output; serializeJson(doc, output);
    request->send(200, "application/json", output);
}

void WebManager::handleSetDeviceIdApi(AsyncWebServerRequest* request) {
    if (request->hasParam("id", true)) {
        _modeManager->updateDeviceId(request->getParam("id", true)->value().toInt(), true);
    }
    request->send(200);
}

void WebManager::handleRunTestApi(AsyncWebServerRequest* request) {
    uint32_t delayMs = NVS::loadTestDelay();
    uint32_t playMs = NVS::loadTestPlay();

    if (request->hasParam("delay", true)) {
        delayMs = request->getParam("delay", true)->value().toInt();
    }
    if (request->hasParam("play", true)) {
        playMs = request->getParam("play", true)->value().toInt();
    }
    
    NVS::saveTestDelay(delayMs);
    NVS::saveTestPlay(playMs);

    if (_modeManager) {
        _modeManager->triggerManualRun(delayMs, playMs);
    }
    request->send(200, "application/json", "{\"status\":\"started\"}");
}


// --- 내부 로직 및 태스크 ---
void WebManager::startSoftAP() {
    WiFi.softAPConfig(AP_IP, AP_IP, IPAddress(255, 255, 255, 0));
    WiFi.softAP(AP_SSID, AP_PASSWORD);
}

void WebManager::wifiScanTask(void* pvParameters) {
    if (isScanning) {
        Log::Warn("WIFI: Scan is already in progress. Ignoring new request.");
        vTaskDelete(NULL);
        return;
    }
    isScanning = true;

    WebManager* self = static_cast<WebManager*>(pvParameters);
    Log::Debug("WiFi Scan Task started.");
    
    // [FIX] More robust reset before scanning
    WiFi.disconnect(true, true);
    WiFi.mode(WIFI_OFF);
    vTaskDelay(pdMS_TO_TICKS(100));
    WiFi.mode(WIFI_AP_STA);
    self->startSoftAP(); // Re-enable AP mode after turning WiFi off/on
    
    WiFi.scanDelete();
    int16_t n = WiFi.scanNetworks(false, false, false, 300, 0);
    
    while (WiFi.scanComplete() == WIFI_SCAN_RUNNING) {
        vTaskDelay(pdMS_TO_TICKS(50));
    }
    n = WiFi.scanComplete();
    
    DynamicJsonDocument doc(JSON_DOC_SIZE_WIFI_SCAN);
    JsonArray networksArray = doc.createNestedArray("networks");
    if (n > 0) {
        Log::Debug("Found %d networks.", n);
        std::vector<std::pair<int, String>> networks;
        for (int i = 0; i < n; ++i) {
            if (WiFi.SSID(i).length() > 0) {
                networks.push_back({WiFi.RSSI(i), WiFi.SSID(i)});
            }
        }
        std::sort(networks.rbegin(), networks.rend());
        for (int i = 0; i < std::min((int)networks.size(), 20); ++i) {
            JsonObject netObj = networksArray.createNestedObject();
            netObj["ssid"] = networks[i].second;
            netObj["rssi"] = networks[i].first;
        }
    } else {
        Log::Warn("WiFi Scan finished, but no networks found (result code: %d).", n);
    }
    
    doc["type"] = "wifi_scan_result";
    String output; serializeJson(doc, output);
    self->_ws.textAll(output);

    WiFi.scanDelete();
    Log::Debug("WiFi Scan Task finished.");
    isScanning = false;
    vTaskDelete(NULL);
}


void WebManager::otaCheckVersionTask(void* pvParameters) {
    WebManager* self = static_cast<WebManager*>(pvParameters);
    self->fetchOtaVersionInfo(); self->broadcastStatusUpdate();
    vTaskDelete(NULL);
}

void WebManager::otaDownloadTask(void* pvParameters) {
    static_cast<WebManager*>(pvParameters)->downloadAndApplyOta();
    vTaskDelete(NULL);
}

bool WebManager::fetchOtaVersionInfo() {
    if (WiFi.status() != WL_CONNECTED) return false;
    WiFiClientSecure client; client.setInsecure(); HTTPClient http;
    http.setConnectTimeout(OTA_HTTP_TIMEOUT_MS);
    if (http.begin(client, OTA_VERSION_URL)) {
        int httpCode = http.GET();
        if (httpCode == HTTP_CODE_OK) {
            DynamicJsonDocument doc(512);
            if (deserializeJson(doc, http.getStream()) == DeserializationError::Ok) {
                xSemaphoreTake(_otaDataMutex, portMAX_DELAY);
                _latestOtaVersion = doc["latest"].as<String>();
                _otaChangeLog = doc["changelog"].as<String>();
                _otaUpdateAvailable = isVersionNewer(_latestOtaVersion, _currentFirmwareVersion);
                xSemaphoreGive(_otaDataMutex);
                http.end();
                return true;
            }
        }
        http.end();
    }
    return false;
}

void WebManager::downloadAndApplyOta() {
    if (esp_task_wdt_add(NULL) != ESP_OK) {
        Log::Error("OTA: Failed to add task to WDT");
    }

    DynamicJsonDocument doc(JSON_DOC_SIZE_API_RESP);
    doc["type"] = "ota_result";

    if (WiFi.status() != WL_CONNECTED) {
        doc["msg"] = "OTA Failed: No Internet";
    } else {
        WiFiClientSecure client;
        client.setInsecure();
        HTTPClient http;
        http.setConnectTimeout(OTA_HTTP_TIMEOUT_MS);

        if (http.begin(client, OTA_FIRMWARE_URL)) {
            int httpCode = http.GET();
            if (httpCode == HTTP_CODE_OK) {
                int contentLength = http.getSize();
                if (contentLength <= 0) {
                    doc["msg"] = "OTA Failed: Invalid content length.";
                } else if (!Update.begin(contentLength)) {
                    doc["msg"] = "OTA Failed: Not enough space. Error: " + String(Update.getError());
                } else {
                    uint8_t buff[1024] = {0};
                    int written = 0;
                    WiFiClient* stream = http.getStreamPtr();
                    Log::Info("OTA: Starting download. Size: %d bytes.", contentLength);

                    while (http.connected() && (written < contentLength)) {
                        esp_task_wdt_reset();
                        int len = stream->readBytes(buff, sizeof(buff));
                        if (len > 0) {
                            if (Update.write(buff, len) != len) {
                                doc["msg"] = "OTA Failed: Write error #" + String(Update.getError());
                                Update.abort();
                                break;
                            }
                            written += len;
                            int progress = (int)(((float)written / (float)contentLength) * 100);
                            static int lastProgress = -1;
                            if (progress > lastProgress) {
                                broadcastOtaProgress(progress);
                                lastProgress = progress;
                            }
                        }
                        delay(5);
                    }

                    if (written == contentLength && Update.end(true)) {
                        if (Update.isFinished()) {
                            _otaUpdateDownloaded = true;
                            if (_modeManager) _modeManager->setUpdateDownloaded(true);
                            doc["msg"] = "Download OK. Exit Wi-Fi mode to apply.";
                             Log::Info("OTA: Download successful.");
                        } else {
                            doc["msg"] = "OTA Failed: Update not finished.";
                        }
                    } else if (!doc.containsKey("msg")) {
                        doc["msg"] = "OTA Failed: Error #" + String(Update.getError());
                        Log::Error("OTA: Download failed. Written: %d, Total: %d, Update Error: %d", written, contentLength, Update.getError());
                    }
                }
            } else {
                doc["msg"] = "OTA Failed: HTTP Error " + String(httpCode);
            }
            http.end();
        } else {
            doc["msg"] = "OTA Failed: Could not connect to server.";
        }
    }

    String output;
    serializeJson(doc, output);
    _ws.textAll(output);
    
    esp_task_wdt_delete(NULL);
}


void WebManager::setupLogBroadcaster() {
    Log::setWebSocketLogSender([this](const String& msg, const char* level) {
        if (_isServerRunning) {
            StaticJsonDocument<JSON_DOC_SIZE_WS_LOG> doc;
            doc["type"] = "log";
            doc["level"] = level;
            doc["msg"] = msg;
            doc["ts"] = millis();
            String wsOutput;
            serializeJson(doc, wsOutput);
            _ws.textAll(wsOutput);
        }
    });
}
void WebManager::broadcastStatusUpdate() {
    DynamicJsonDocument doc(JSON_DOC_SIZE_STATUS);
    doc["type"] = "ota_status";
    xSemaphoreTake(_otaDataMutex, portMAX_DELAY);
    doc["current_version"] = _currentFirmwareVersion;
    doc["latest_version"] = _latestOtaVersion;
    doc["changelog"] = _otaChangeLog;
    doc["update_available"] = _otaUpdateAvailable;
    xSemaphoreGive(_otaDataMutex);
    doc["internet_ok"] = (WiFi.status() == WL_CONNECTED);
    String output; serializeJson(doc, output);
    _ws.textAll(output);
}

String WebManager::getPageHeader(const String& title) {
    String html = F("<!DOCTYPE html><html lang='en'><head><meta charset='UTF-8'><meta name='viewport' content='width=device-width, initial-scale=1.0'>");
    html += "<title>" + title + "</title>";
    html += F(R"rawliteral(<style>
        body{font-family:-apple-system,BlinkMacSystemFont,'Segoe UI',Roboto,Helvetica,Arial,sans-serif;margin:0;padding:10px;background-color:#f0f2f5;color:#1c1e21;text-align:center;}
        .container{max-width:800px;margin:auto;background:white;padding:20px;border-radius:8px;box-shadow:0 2px 4px rgba(0,0,0,0.1);}
        h1,h2,h3{color:#1c1e21; border-bottom: 1px solid #ddd; padding-bottom: 10px; margin-top:0;}
        .btn{display:inline-block;background-color:#1877f2;color:white;padding:10px 15px;margin:5px;text-decoration:none;border:none;border-radius:6px;cursor:pointer;font-size:16px;font-weight:bold;min-width:150px;transition:background-color 0.2s;}
        .btn:hover{background-color:#166fe5;} .btn:disabled{background-color:#9dbfec; cursor: not-allowed;}
        .btn-danger{background-color:#fa383e;}.btn-danger:hover{background-color:#e0282e;}
        input[type='text'],input[type='password'],input[type='number'],select{width:calc(100% - 22px);padding:12px;margin:8px 0;border:1px solid #dddfe2;border-radius:6px;box-sizing:border-box;font-size:16px;}
        .card{background:#fff;padding:20px;margin-bottom:20px;border-radius:8px;box-shadow:0 1px 2px rgba(0,0,0,0.1);}
        .hidden{display:none;}
    </style></head><body><div class='container'><h1>)rawliteral");
    html += title; html += F("</h1>"); return html;
}
String WebManager::getPageFooter(bool showHomeButton) {
    String html; if (showHomeButton) html += F("<p style='margin-top:20px;'><a href='/' class='btn'>Back to Home</a></p>");
    html += F("</div></body></html>"); return html;
}
