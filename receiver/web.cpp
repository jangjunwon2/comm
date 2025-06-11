// jangjunwon2/comm/comm-6295635354ffa5ad160f5b3be2c0db2652b69d97/main/web.cpp
// =========================================================================
// web.cpp
// =========================================================================

/**
 * @file web.cpp
 * @brief WebManager 클래스의 구현입니다.
 * @version 7.6.0
 * @date 2024-06-13
 */
#include "web.h" //
#include "mode.h" //
#include <WiFi.h> //
#include <WiFiClientSecure.h> //
#include <HTTPClient.h> //
#include <Update.h> //
#include <vector> //
#include <algorithm> //
#include <esp_task_wdt.h> //

WebManager::WebManager() :
    _server(80), _ws("/ws"), _modeManager(nullptr), _isServerRunning(false),
    _otaUpdateDownloaded(false), _currentFirmwareVersion(FIRMWARE_VERSION), //
    _latestOtaVersion("N/A"), _otaChangeLog("N/A"), _otaUpdateAvailable(false) { //
    _otaDataMutex = xSemaphoreCreateMutex(); //
}

void WebManager::begin(ModeManager* modeMgr) {
    _modeManager = modeMgr; //
    setupRoutes(); //
    setupLogBroadcaster(); //
    Log::Info(PSTR("WEB: WebManager initialized.")); //
}

void WebManager::startServer() {
    if (_isServerRunning) return; //
    Log::Info(PSTR("WEB: Starting web server...")); //
    WiFi.mode(WIFI_AP_STA); //
    startSoftAP(); //
    String storedSsid = NVS::loadWifiSsid(); //
    if (!storedSsid.isEmpty()) { //
        WiFi.begin(storedSsid.c_str(), NVS::loadWifiPassword().c_str()); //
    }
    _server.begin(); //
    _isServerRunning = true; //
    _otaUpdateDownloaded = false; //
    if (_modeManager) _modeManager->setUpdateDownloaded(false); //
    Log::Info(PSTR("WEB: Server started. AP IP: http://%s"), WiFi.softAPIP().toString().c_str()); //
}

void WebManager::stopServer() {
    if (!_isServerRunning) return; //
    Log::Info(PSTR("WEB: Stopping web server...")); //
    _ws.closeAll(); //
    _server.end(); //
    WiFi.softAPdisconnect(true); //
    _isServerRunning = false; //
    Log::Info(PSTR("WEB: Server stopped.")); //
}

bool WebManager::isServerRunning() const { return _isServerRunning; } //
void WebManager::performUpdate() { if (_otaUpdateDownloaded) ESP.restart(); } //
void WebManager::broadcastTestComplete() { _ws.textAll("{\"type\":\"test_completed\"}"); } //
void WebManager::broadcastOtaProgress(int progress) { //
    StaticJsonDocument<128> doc; //
    doc["type"] = "ota_progress"; //
    doc["progress"] = progress; //
    String output; //
    serializeJson(doc, output); //
    _ws.textAll(output); //
}

void WebManager::broadcastWifiStatus() {
    DynamicJsonDocument doc(JSON_DOC_SIZE_API_RESP); //
    doc["type"] = "wifi_status_update"; //
    doc["connected"] = (WiFi.status() == WL_CONNECTED); //
    if (doc["connected"]) { //
        doc["ssid"] = WiFi.SSID(); //
        doc["ip"] = WiFi.localIP().toString(); //
    } else { //
        doc["ssid"] = "N/A"; //
        doc["ip"] = "0.0.0.0"; //
    }
    String output; //
    serializeJson(doc, output); //
    _ws.textAll(output); //
}


void WebManager::setupRoutes() {
    _server.on("/", HTTP_GET, [this](AsyncWebServerRequest* r){ handleRoot(r); }); //
    _server.on("/wifi", HTTP_GET, [this](AsyncWebServerRequest* r){ handleWifiConfigPage(r); }); //
    _server.on("/update", HTTP_GET, [this](AsyncWebServerRequest* r){ handleFirmwareUpdatePage(r); }); //
    _server.on("/test", HTTP_GET, [this](AsyncWebServerRequest* r){ handleTestModePage(r); }); //
    _server.on("/exit", HTTP_GET, [this](AsyncWebServerRequest* r){ handleExit(r); }); //
    _server.on("/api/scanwifi", HTTP_GET, [this](AsyncWebServerRequest* r){ handleScanWifiApi(r); }); //
    _server.on("/api/connectwifi", HTTP_POST, [this](AsyncWebServerRequest* r){ handleConnectWifiApi(r); }); //
    _server.on("/api/wifistatus", HTTP_GET, [this](AsyncWebServerRequest* r){ handleWifiStatusApi(r); }); //
    _server.on("/api/checkota", HTTP_GET, [this](AsyncWebServerRequest* r){ handleCheckOtaApi(r); }); //
    _server.on("/api/downloadota", HTTP_POST, [this](AsyncWebServerRequest* r){ handleDownloadOtaApi(r); }); //
    _server.on("/api/devicestatus", HTTP_GET, [this](AsyncWebServerRequest* r){ handleDeviceStatusApi(r); }); //
    _server.on("/api/setdeviceid", HTTP_POST, [this](AsyncWebServerRequest* r){ handleSetDeviceIdApi(r); }); //
    _server.on("/api/runtest", HTTP_POST, [this](AsyncWebServerRequest* r){ handleRunTestApi(r); }); //
    _server.onNotFound([this](AsyncWebServerRequest* r){ handleNotFound(r); }); //
    _ws.onEvent([this](auto *s, auto *c, AwsEventType t, void *a, uint8_t *d, size_t l) { onWsEvent(s, c, t, a, d, l); }); //
    _server.addHandler(&_ws); //
}

void WebManager::onWsEvent(AsyncWebSocket *server, AsyncWebSocketClient *client, AwsEventType type, void *arg, uint8_t *data, size_t len) {
    if (_modeManager) _modeManager->recordWebApiActivity(); //
    if (type == WS_EVT_CONNECT) { //
        Log::Debug(PSTR("WEB: WebSocket client #%u connected."), client->id()); //
        broadcastStatusUpdate(); //
        broadcastWifiStatus(); // [NEW] Send WiFi status on connect
    } else if (type == WS_EVT_DISCONNECT) { //
        Log::Debug(PSTR("WEB: WebSocket client #%u disconnected."), client->id()); //
    }
}

// --- Page Handlers ---

void WebManager::handleRoot(AsyncWebServerRequest* request) {
    _modeManager->recordWebApiActivity(); //
    String html = getPageHeader("Mystic Lab Device"); //
    html += "<div class='card'><h3>Wi-Fi Status</h3><p id='home-wifi-status'>Loading...</p></div>"; // [NEW] Add WiFi status section
    html += "<div class='card'><h3>Device Control</h3>" //
            "<p><a href='/wifi' class='btn'>Wi-Fi Settings</a></p>" //
            "<p><a href='/update' class='btn'>Firmware Update</a></p>" //
            "<p><a href='/test' class='btn'>Test Mode</a></p>" //
            "<p><a href='/exit' class='btn btn-danger'>Exit Wi-Fi Mode</a></p>" //
            "</div>";
    html += getPageFooter(false); //
    html += R"rawliteral(
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
    )rawliteral"; // [NEW] JavaScript for home page WiFi status
    request->send(200, "text/html; charset=UTF-8", html); //
}

void WebManager::handleWifiConfigPage(AsyncWebServerRequest* request) {
    _modeManager->recordWebApiActivity(); //
    String html = getPageHeader("Wi-Fi Settings"); //
    html += R"rawliteral(
        <div class='card'>
            <h3>Current Status</h3>
            <p id='wifi-status'>Loading...</p>
        </div>
        <div class='card'>
            <h3>Connect to a Network <button onclick='scanWifi()' id='scan-btn' class='btn' style='font-size:12px; padding: 5px 10px; min-width:auto; float:right;'>Scan Again</button></h3>
            <form id='connect-form' onsubmit='return connectToWifi(event)'>
                <label for='ssid-select'>Select Network:</label>
                <select id='ssid-select' name='ssid'></select>
                <label for='password'>Password:</label>
                <input type='password' id='password' name='password' placeholder='Enter password (if any)'>
                <p><input type='submit' id='connect-btn' value='Save & Connect' class='btn'></p>
            </form>
            <div id='connect-result' style='margin-top:10px; font-weight:bold;'></div>
        </div>
        <script>
            let ws;
            let pollIntervalId = null; // To store the interval ID
            function connectWs(){
                ws = new WebSocket("ws://"+window.location.host+"/ws");
                ws.onopen = () => { console.log("WebSocket connected!"); fetchStatus(); scanWifi(); };
                ws.onmessage = e => {
                    try { 
                        const d = JSON.parse(e.data); 
                        if (d.type === "wifi_scan_result") { 
                            updateWifiList(d.networks); 
                        } else if (d.type === "wifi_status_update") { // Handle real-time status updates
                            updateWifiStatusDisplay(d);
                            if (d.connected || d.status === 'failed') { // If connected or failed, stop polling
                                clearInterval(pollIntervalId);
                                pollIntervalId = null;
                                let r = document.getElementById("connect-result");
                                let b = document.getElementById("connect-btn");
                                if (d.connected) {
                                    r.innerHTML = "<p style='color:green;'>Connection Successful!<br>SSID: " + d.ssid + "<br>IP: " + d.ip + "<br>Returning to home in 3 seconds...</p>";
                                    setTimeout(() => { window.location.href = '/'; }, 3000);
                                } else if (d.status === 'failed') {
                                    r.innerHTML = "<p style='color:red;'>Connection Failed. Please check your password and try again.</p>";
                                    b.disabled = false;
                                }
                            }
                        }
                    } catch(err) { console.error("WS message error:", err); }
                };
                ws.onclose = () => { console.log("WebSocket disconnected. Reconnecting..."); setTimeout(connectWs, 2000); };
            }
            function scanWifi() {
                let s=document.getElementById("ssid-select"), b=document.getElementById("scan-btn");
                s.innerHTML = "<option value=''>Scanning...</option>"; b.disabled = true;
                fetch("/api/scanwifi");
            }
            function fetchStatus(){
                fetch("/api/wifistatus")
                .then(r=>r.json())
                .then(d=>{
                    updateWifiStatusDisplay(d);
                });
            }
            function updateWifiStatusDisplay(d){
                let s=document.getElementById("wifi-status");
                if(d.connected){
                    s.innerHTML="Connected to <b>"+d.ssid+"</b><br>IP Address: "+d.ip;
                } else if (d.status === 'connecting') {
                     s.innerHTML="Attempting to connect to <b>" + d.ssid + "</b>...";
                } else {
                    s.textContent="Not connected. AP Mode is active.";
                }
            }
            function updateWifiList(nets){
                let s=document.getElementById("ssid-select"), b=document.getElementById("scan-btn");
                s.innerHTML = "<option value=''>-- Select a Network --</option>";
                if(nets && nets.length > 0) nets.forEach(net=>{ s.innerHTML += `<option value='`+net.ssid+`'>`+net.ssid+` (`+net.rssi+` dBm)</option>`; });
                else s.innerHTML = "<option value=''>No networks found</option>";
                b.disabled = false;
            }
            function connectToWifi(e){
                e.preventDefault();
                let r=document.getElementById("connect-result"), b=document.getElementById("connect-btn");
                r.innerHTML="<p>Starting connection attempt...</p>"; b.disabled = true;

                fetch("/api/connectwifi",{method:"POST",body:new FormData(e.target)})
                .then(resp => resp.json())
                .then(data => {
                    if (data.status !== 'connection_started') {
                        r.innerHTML = "<p style='color:red;'>Failed to start connection: " + (data.error || "Unknown error") + "</p>";
                        b.disabled = false;
                        return;
                    }
                    r.innerHTML = "<p>Connecting... Please wait. This could take up to 15 seconds.</p>";
                    let pollCount = 0;
                    let fastPolls = 5; // Poll faster for initial 5 seconds (1s intervals)
                    let normalPolls = 5; // Then normal polls for remaining 10 seconds (2s intervals)
                    let currentSsid = e.target.ssid.value; // Get the SSID being connected to

                    // Clear any existing poll interval
                    if (pollIntervalId) clearInterval(pollIntervalId);

                    pollIntervalId = setInterval(() => {
                        fetch("/api/wifistatus")
                        .then(statusResp => statusResp.json())
                        .then(statusData => {
                            updateWifiStatusDisplay(statusData); // Update status in real-time
                            pollCount++;
                            if (statusData.connected) {
                                clearInterval(pollIntervalId);
                                pollIntervalId = null;
                                r.innerHTML = "<p style='color:green;'>Connection Successful!<br>SSID: " + statusData.ssid + "<br>IP: " + statusData.ip + "<br>Returning to home in 3 seconds...</p>";
                                setTimeout(() => { window.location.href = '/'; }, 3000);
                            } else if (statusData.status === 'failed' || (pollCount > (fastPolls + normalPolls) && !statusData.connected && statusData.ssid !== currentSsid)) { // Check for explicit failure or timeout and not connecting to chosen SSID
                                clearInterval(pollIntervalId);
                                pollIntervalId = null;
                                r.innerHTML = "<p style='color:red;'>Connection Failed. Please check your password and try again.</p>";
                                b.disabled = false;
                            } else if (pollCount <= fastPolls) { // Fast polling for initial attempts
                                // Do nothing, interval will loop quickly
                            } else if (pollCount > fastPolls && pollCount <= (fastPolls + normalPolls)) { // Slower polling after initial fast attempts
                                // Do nothing, interval will loop at normal speed
                            } else {
                                // Fallback for very long connections, continue polling or timeout eventually
                                // This block implicitly handles the 15-second timeout by allowing more polls.
                                // The statusData.ssid !== currentSsid helps detect if it failed and connected to something else (e.g. AP)
                            }
                        }).catch(err => {
                            console.error("Polling error:", err);
                            // If API call fails, assume device might be restarting or unreachable
                            clearInterval(pollIntervalId);
                            pollIntervalId = null;
                            r.innerHTML = "<p style='color:red;'>Error communicating with device. The device may have restarted or lost connection. Please try reconnecting to the device's Wi-Fi network and retry.</p>";
                            b.disabled = false;
                        });
                    }, (pollCount < fastPolls) ? 1000 : 2000); // 1 sec for fastPolls, then 2 sec
                }).catch(err => {
                    r.innerHTML = "<p style='color:red;'>Error sending command. The device may have disconnected. Please try reconnecting to the device's Wi-Fi network.</p>";
                    b.disabled = false;
                });

                return false;
            }
            window.onload = connectWs;
        </script>
    )rawliteral";
    html += getPageFooter(true); //
    request->send(200, "text/html; charset=UTF-8", html); //
}

void WebManager::handleFirmwareUpdatePage(AsyncWebServerRequest* request) {
    _modeManager->recordWebApiActivity(); //
    String html = getPageHeader("Firmware Update"); //
    html += R"rawliteral(
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
    )rawliteral";
    html += getPageFooter(true); //
    request->send(200, "text/html; charset=UTF-8", html); //
}

void WebManager::handleTestModePage(AsyncWebServerRequest* request) {
    if (_modeManager) _modeManager->switchToMode(DeviceMode::MODE_TEST, false); //
    String html = getPageHeader("Test Mode"); //
    html += R"rawliteral(
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
            function saveId(){fetch("/api/setdeviceid",{method:"POST",headers:{"Content-Type":"application/x-www-form-urlencoded"},body:"id="+document.getElementById("dev-id").value}).then(()=>alert('ID Saved!'));}
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
                        // 간소화된 로그 형식 처리
                        if (d.level === "TEST") { // Check for the new simplified log level
                            log.innerHTML+=`<div style="color:#fff;">${d.msg}</div>`;
                        } else {
                            log.innerHTML+=`<div><span style="color:#888;">[${d.ts}]</span><span style="color:#0f0;">[${d.level}]</span> ${d.msg}</div>`;
                        }
                        log.scrollTop=log.scrollHeight;
                    }
                    if(d.type==="test_completed"){
                         let btn = document.getElementById('run-test-btn');
                         btn.disabled = false;
                         btn.textContent = 'Run Manual Test';
                         log.innerHTML+=`<div style="color:#fff;">Test Completed.</div>`; // Add a simplified message
                    }
                }catch(e){}
            };
            window.onload=getStatus;
        </script>
    )rawliteral";
    html += getPageFooter(true); //
    request->send(200, "text/html; charset=UTF-8", html); //
}

void WebManager::handleExit(AsyncWebServerRequest* request) {
    String html = getPageHeader("Exiting Wi-Fi Mode"); //
    html += "<p>The device will now return to normal operation. You can close this window.</p>"; //
    if (_otaUpdateDownloaded) { //
        html += "<p style='color:blue;font-weight:bold;'>An update was downloaded and will be applied on reboot.</p>"; //
    }
    html += getPageFooter(false); //
    request->send(200, "text/html; charset=UTF-8", html); //
    delay(100); //
    if (_modeManager) _modeManager->exitWifiMode(); //
}

void WebManager::handleNotFound(AsyncWebServerRequest* request) { request->send(404, "text/plain", "Not Found"); } //

// --- API 핸들러 ---
void WebManager::handleScanWifiApi(AsyncWebServerRequest* request) {
    xTaskCreate(wifiScanTask, "wifiScanTask", 4096, this, 5, NULL); //
    request->send(200, "application/json", "{\"status\":\"scan_started\"}"); //
}

void WebManager::handleConnectWifiApi(AsyncWebServerRequest* request) {
    String ssid, pass; //
    if (request->hasParam("ssid", true)) ssid = request->getParam("ssid", true)->value(); //
    if (request->hasParam("password", true)) pass = request->getParam("password", true)->value(); //
    
    DynamicJsonDocument doc(JSON_DOC_SIZE_API_RESP); //
    if (ssid.length() > 0) { //
        NVS::saveWifiSsid(ssid); //
        NVS::saveWifiPassword(pass); //
        Log::Info(PSTR("WEB: Attempting to connect to SSID: %s"), ssid.c_str()); //
        WiFi.begin(ssid.c_str(), pass.c_str()); //
        doc["status"] = "connection_started"; //
        // [NEW] Immediately check status after WiFi.begin()
        if (WiFi.status() == WL_CONNECTED) { //
            doc["connected"] = true; //
            doc["ssid"] = WiFi.SSID(); //
            doc["ip"] = WiFi.localIP().toString(); //
            Log::Info(PSTR("WEB: WiFi connected successfully during initial check.")); //
        } else if (WiFi.status() == WL_NO_SSID_AVAIL || WiFi.status() == WL_CONNECT_FAILED) { //
             doc["status"] = "failed"; //
             doc["error"] = "Connection failed immediately."; //
             Log::Warn(PSTR("WEB: WiFi connection failed immediately: %d"), WiFi.status()); //
        } else { //
            doc["status"] = "connecting"; //
        }
    } else { //
        doc["status"] = "fail"; //
        doc["error"] = "Missing SSID"; //
    }
    String output; //
    serializeJson(doc, output); //
    request->send(200, "application/json", output); //
}

void WebManager::handleWifiStatusApi(AsyncWebServerRequest* request) {
    DynamicJsonDocument doc(JSON_DOC_SIZE_API_RESP); //
    doc["connected"] = (WiFi.status() == WL_CONNECTED); //
    if (doc["connected"]) { //
        doc["ssid"] = WiFi.SSID(); //
        doc["ip"] = WiFi.localIP().toString(); //
        doc["status"] = "connected"; //
    } else if (WiFi.status() == WL_IDLE_STATUS || WiFi.status() == WL_NO_SSID_AVAIL || WiFi.status() == WL_SCAN_COMPLETED || WiFi.status() == WL_CONNECT_FAILED){ //
        // Explicitly handle failure states for faster feedback
        doc["status"] = "failed"; //
        doc["ssid"] = NVS::loadWifiSsid(); // // Show attempted SSID
        doc["error"] = "Not connected or connection failed."; //
    } else { //
        doc["status"] = "connecting"; //
        doc["ssid"] = NVS::loadWifiSsid(); // // Show attempted SSID
    }
    String output; serializeJson(doc, output); //
    request->send(200, "application/json", output); //
}

void WebManager::handleCheckOtaApi(AsyncWebServerRequest* request) {
    xTaskCreate(otaCheckVersionTask, "otaCheckTask", 4096, this, 5, NULL); //
    request->send(200, "application/json", "{\"status\":\"checking\"}"); //
}

void WebManager::handleDownloadOtaApi(AsyncWebServerRequest* request) {
    // Increased stack size for OTA download task, as it involves network operations and file writing
    xTaskCreate(otaDownloadTask, "otaDownloadTask", 10240, this, 2, NULL); // [MODIFIED] Increased stack from 8192 to 10240 //
    request->send(200, "application/json", "{\"status\":\"download_started\"}"); //
}

void WebManager::handleDeviceStatusApi(AsyncWebServerRequest* request) {
    DynamicJsonDocument doc(JSON_DOC_SIZE_API_RESP); //
    doc["device_id"] = NVS::loadDeviceId(); //
    doc["test_delay_ms"] = NVS::loadTestDelay(); //
    doc["test_play_ms"] = NVS::loadTestPlay(); //
    String output; serializeJson(doc, output); //
    request->send(200, "application/json", output); //
}

void WebManager::handleSetDeviceIdApi(AsyncWebServerRequest* request) {
    if (request->hasParam("id", true)) _modeManager->updateDeviceId(request->getParam("id", true)->value().toInt(), true); //
    request->send(200); //
}

void WebManager::handleRunTestApi(AsyncWebServerRequest* request) {
    uint32_t delayMs = NVS::loadTestDelay(); //
    uint32_t playMs = NVS::loadTestPlay(); //

    if (request->hasParam("delay", true)) { //
        delayMs = request->getParam("delay", true)->value().toInt(); //
    }
    if (request->hasParam("play", true)) { //
        playMs = request->getParam("play", true)->value().toInt(); //
    }

    if (_modeManager) { //
        _modeManager->triggerManualRun(delayMs, playMs); //
    }
    request->send(200, "application/json", "{\"status\":\"started\"}"); //
}


// --- 내부 로직 및 태스크 ---
void WebManager::startSoftAP() {
    WiFi.softAPConfig(AP_IP, AP_IP, IPAddress(255, 255, 255, 0)); //
    WiFi.softAP(AP_SSID, AP_PASSWORD); //
}

void WebManager::wifiScanTask(void* pvParameters) {
    WebManager* self = static_cast<WebManager*>(pvParameters); //
    Log::Debug("WiFi Scan Task started."); //
    int n = WiFi.scanNetworks(); //
    DynamicJsonDocument doc(JSON_DOC_SIZE_WIFI_SCAN); //
    JsonArray networksArray = doc.createNestedArray("networks"); //
    if (n > 0) { //
        std::vector<std::pair<int, String>> networks; //
        for (int i = 0; i < n; ++i) if (WiFi.SSID(i).length() > 0) networks.push_back({WiFi.RSSI(i), WiFi.SSID(i)}); //
        std::sort(networks.rbegin(), networks.rend()); //
        for (int i = 0; i < std::min((int)networks.size(), 20); ++i) { //
            JsonObject netObj = networksArray.createNestedObject(); //
            netObj["ssid"] = networks[i].second; //
            netObj["rssi"] = networks[i].first; //
        }
    }
    doc["type"] = "wifi_scan_result"; //
    String output; serializeJson(doc, output); //
    self->_ws.textAll(output); //
    Log::Debug("WiFi Scan Task finished, found %d networks.", n); //
    vTaskDelete(NULL); //
}

void WebManager::otaCheckVersionTask(void* pvParameters) {
    WebManager* self = static_cast<WebManager*>(pvParameters); //
    self->fetchOtaVersionInfo(); self->broadcastStatusUpdate(); //
    vTaskDelete(NULL); //
}

void WebManager::otaDownloadTask(void* pvParameters) {
    static_cast<WebManager*>(pvParameters)->downloadAndApplyOta(); //
    vTaskDelete(NULL); //
}

bool WebManager::fetchOtaVersionInfo() {
    if (WiFi.status() != WL_CONNECTED) return false; //
    WiFiClientSecure client; client.setInsecure(); HTTPClient http; //
    http.setConnectTimeout(OTA_HTTP_TIMEOUT_MS); //
    if (http.begin(client, OTA_VERSION_URL)) { //
        int httpCode = http.GET(); //
        if (httpCode == HTTP_CODE_OK) { //
            DynamicJsonDocument doc(512); //
            if (deserializeJson(doc, http.getStream()) == DeserializationError::Ok) { //
                xSemaphoreTake(_otaDataMutex, portMAX_DELAY); //
                _latestOtaVersion = doc["latest"].as<String>(); //
                _otaChangeLog = doc["changelog"].as<String>(); //
                _otaUpdateAvailable = isVersionNewer(_latestOtaVersion, _currentFirmwareVersion); //
                xSemaphoreGive(_otaDataMutex); //
                http.end(); //
                return true; //
            }
        }
        http.end(); //
    }
    return false; //
}

void WebManager::downloadAndApplyOta() {
    if (esp_task_wdt_add(NULL) != ESP_OK) { //
        Log::Error("OTA: Failed to add task to WDT"); //
    }

    DynamicJsonDocument doc(JSON_DOC_SIZE_API_RESP); //
    doc["type"] = "ota_result"; //

    if (WiFi.status() != WL_CONNECTED) { //
        doc["msg"] = "OTA Failed: No Internet"; //
    } else { //
        WiFiClientSecure client; //
        client.setInsecure(); //
        HTTPClient http; //
        http.setConnectTimeout(OTA_HTTP_TIMEOUT_MS); //

        if (http.begin(client, OTA_FIRMWARE_URL)) { //
            int httpCode = http.GET(); //
            if (httpCode == HTTP_CODE_OK) { //
                int contentLength = http.getSize(); //
                if (contentLength <= 0) { //
                    doc["msg"] = "OTA Failed: Invalid content length."; //
                } else if (!Update.begin(contentLength)) { //
                    doc["msg"] = "OTA Failed: Not enough space. Error: " + String(Update.getError()); //
                } else { //
                    uint8_t buff[1024] = {0}; //
                    int written = 0; //
                    WiFiClient* stream = http.getStreamPtr(); //
                    Log::Info("OTA: Starting download. Size: %d bytes.", contentLength); //

                    while (http.connected() && (written < contentLength)) { //
                        esp_task_wdt_reset(); //
                        int len = stream->readBytes(buff, sizeof(buff)); //
                        if (len > 0) { //
                            if (Update.write(buff, len) != len) { //
                                doc["msg"] = "OTA Failed: Write error #" + String(Update.getError()); //
                                Update.abort(); //
                                break; //
                            }
                            written += len; //
                            int progress = (int)(((float)written / (float)contentLength) * 100); //
                            static int lastProgress = -1; //
                            if (progress > lastProgress) { //
                                broadcastOtaProgress(progress); //
                                lastProgress = progress; //
                            }
                        }
                        delay(5); //
                    }

                    if (written == contentLength && Update.end(true)) { //
                        if (Update.isFinished()) { //
                            _otaUpdateDownloaded = true; //
                            if (_modeManager) _modeManager->setUpdateDownloaded(true); //
                            doc["msg"] = "Download OK. Exit Wi-Fi mode to apply."; //
                             Log::Info("OTA: Download successful."); //
                        } else { //
                            doc["msg"] = "OTA Failed: Update not finished."; //
                        }
                    } else if (!doc.containsKey("msg")) { //
                        doc["msg"] = "OTA Failed: Error #" + String(Update.getError()); //
                        Log::Error("OTA: Download failed. Written: %d, Total: %d, Update Error: %d", written, contentLength, Update.getError()); //
                    }
                }
            } else { //
                doc["msg"] = "OTA Failed: HTTP Error " + String(httpCode); //
            }
            http.end(); //
        } else { //
            doc["msg"] = "OTA Failed: Could not connect to server."; //
        }
    }

    String output; //
    serializeJson(doc, output); //
    _ws.textAll(output); //
    
    esp_task_wdt_delete(NULL); //
}


void WebManager::setupLogBroadcaster() {
    Log::setWebSocketLogSender([this](const String& msg) {
        if (_isServerRunning) {
            // [NEW] Check if the message is a simplified TEST log
            // If the message starts with a specific pattern (e.g., not JSON, or a custom indicator)
            // For now, let's assume the simplified logs from Log::TestLog will be directly formatted.
            // If msg is already a JSON, it will be sent as is.
            // Simplified logs are expected to be pure strings by Log::TestLog and parsed here.
            
            // To differentiate, we can check if it's a JSON object by looking for '{'
            if (msg.startsWith("{")) {
                _ws.textAll(msg);
            } else {
                // If it's a plain string (from Log::TestLog), wrap it in a JSON object for WebSocket
                StaticJsonDocument<JSON_DOC_SIZE_WS_LOG> doc;
                doc["type"] = "log";
                doc["level"] = "TEST"; // Assign a special level for simplified logs
                doc["msg"] = msg;
                String wsOutput;
                serializeJson(doc, wsOutput);
                _ws.textAll(wsOutput);
            }
        }
    });
} //
void WebManager::broadcastStatusUpdate() {
    DynamicJsonDocument doc(JSON_DOC_SIZE_STATUS); //
    doc["type"] = "ota_status"; //
    xSemaphoreTake(_otaDataMutex, portMAX_DELAY); //
    doc["current_version"] = _currentFirmwareVersion; //
    doc["latest_version"] = _latestOtaVersion; //
    doc["changelog"] = _otaChangeLog; //
    doc["update_available"] = _otaUpdateAvailable; //
    xSemaphoreGive(_otaDataMutex); //
    doc["internet_ok"] = (WiFi.status() == WL_CONNECTED); //
    String output; serializeJson(doc, output); //
    _ws.textAll(output); //
}

String WebManager::getPageHeader(const String& title) {
    String html = F("<!DOCTYPE html><html lang='en'><head><meta charset='UTF-8'><meta name='viewport' content='width=device-width, initial-scale=1.0'>"); // [MODIFIED] Changed lang='ko' to lang='en' //
    html += "<title>" + title + "</title>"; //
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
    String html; if (showHomeButton) html += F("<p style='margin-top:20px;'><a href='/' class='btn'>Back to Home</a></p>"); //
    html += F("</div></body></html>"); return html; //
}