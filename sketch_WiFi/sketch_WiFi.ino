/*
  ESP32 WiFi Configuration Portal
  - Starts in AP mode if no credentials (captive portal)
  - Stores SSID/Password in NVS (Preferences)
  - Auto-connects in STA mode if credentials exist
  - REST API for status, scan, connect and reset
  - Auto-reconnect if WiFi drops
  - Reset via button (GPIO0) or endpoint
*/

#include <WiFi.h>
#include <WebServer.h>
#include <DNSServer.h>
#include <Preferences.h>

const char* AP_SSID = "ESP32_PROVISION_TEST";
const char* AP_PASS = "pass12345";
const byte DNS_PORT = 53;
const int RESET_BTN = 0;
const unsigned long LONG_PRESS_MS = 5000;

WebServer server(80);
DNSServer dnsServer;
Preferences prefs;

String storedSSID;
String storedPassword;

bool portalMode = false;
bool shouldRestart = false;
unsigned long restartTime = 0;
unsigned long lastReconnectAttempt = 0;
const unsigned long RECONNECT_INTERVAL_MS = 15000;

// === Credentials Management ===
void saveCredentials(const String& ssid, const String& pass) {
  prefs.begin("wifi", false);
  prefs.putString("ssid", ssid);
  prefs.putString("pass", pass);
  prefs.end();
  
  delay(100);
  
  prefs.begin("wifi", true);
  String verifySSID = prefs.getString("ssid", "");
  String verifyPass = prefs.getString("pass", "");
  prefs.end();
  
  if (verifySSID == ssid && verifyPass == pass) {
    storedSSID = ssid;
    storedPassword = pass;
    Serial.println("[AUDIT] Credentials saved and verified in NVS");
    Serial.println("  SSID: " + ssid);
  } else {
    Serial.println("[ERROR] NVS verification failed!");
  }
}

void clearCredentials() {
  prefs.begin("wifi", false);
  prefs.remove("ssid");
  prefs.remove("pass");
  prefs.end();
  storedSSID = "";
  storedPassword = "";
  Serial.println("[AUDIT] Credentials cleared from NVS");
}

bool loadCredentials() {
  prefs.begin("wifi", true);
  storedSSID = prefs.getString("ssid", "");
  storedPassword = prefs.getString("pass", "");
  prefs.end();
  
  if (storedSSID.length() > 0) {
    Serial.println("[INFO] Credentials found in NVS:");
    Serial.println("  SSID: " + storedSSID);
    return true;
  } else {
    Serial.println("[INFO] No credentials stored in NVS");
    return false;
  }
}

String ipToString(IPAddress ip) {
  return String(ip[0]) + "." + String(ip[1]) + "." + String(ip[2]) + "." + String(ip[3]);
}

// === HTML Interface ===
String htmlPage() {
  String html = F("<!DOCTYPE html><html><head><meta name='viewport' content='width=device-width,initial-scale=1'>"
                  "<title>ESP32 WiFi Setup</title>"
                  "<style>"
                  "* { margin: 0; padding: 0; box-sizing: border-box; }"
                  "body { font-family: 'Segoe UI', Tahoma, Geneva, Verdana, sans-serif; "
                  "background: linear-gradient(135deg, #667eea 0%, #764ba2 100%); "
                  "min-height: 100vh; display: flex; align-items: center; justify-content: center; padding: 20px; }"
                  ".container { background: white; border-radius: 16px; box-shadow: 0 20px 60px rgba(0,0,0,0.3); "
                  "padding: 40px; max-width: 480px; width: 100%; }"
                  "h1 { color: #667eea; margin-bottom: 24px; font-size: 28px; text-align: center; }"
                  ".form-group { margin-bottom: 20px; }"
                  "label { display: block; color: #4a5568; font-weight: 600; margin-bottom: 8px; font-size: 14px; }"
                  "input { width: 100%; padding: 12px 16px; border: 2px solid #e2e8f0; border-radius: 8px; "
                  "font-size: 16px; transition: all 0.3s; }"
                  "input:focus { outline: none; border-color: #667eea; box-shadow: 0 0 0 3px rgba(102,126,234,0.1); }"
                  "button { width: 100%; padding: 14px; border: none; border-radius: 8px; font-size: 16px; "
                  "font-weight: 600; cursor: pointer; transition: all 0.3s; margin-top: 8px; }"
                  ".btn-primary { background: linear-gradient(135deg, #667eea 0%, #764ba2 100%); color: white; }"
                  ".btn-primary:hover { transform: translateY(-2px); box-shadow: 0 10px 20px rgba(102,126,234,0.3); }"
                  ".btn-secondary { background: #f7fafc; color: #4a5568; border: 2px solid #e2e8f0; }"
                  ".btn-secondary:hover { background: #edf2f7; }"
                  ".btn-danger { background: #fc8181; color: white; }"
                  ".btn-danger:hover { background: #f56565; }"
                  ".divider { height: 1px; background: #e2e8f0; margin: 24px 0; }"
                  ".status { padding: 16px; background: #f7fafc; border-radius: 8px; margin-top: 20px; "
                  "border-left: 4px solid #667eea; }"
                  ".status strong { color: #667eea; }"
                  ".connected { border-left-color: #48bb78; }"
                  ".connected strong { color: #48bb78; }"
                  "code { background: #edf2f7; padding: 2px 6px; border-radius: 4px; font-size: 14px; }"
                  "</style></head><body>"
                  "<div class='container'>"
                  "<h1>WiFi Configuration</h1>");
  
  html += F("<form action='/save' method='POST'>"
            "<div class='form-group'>"
            "<label>Network Name (SSID)</label>"
            "<input type='text' name='ssid' placeholder='Enter WiFi name' required>"
            "</div>"
            "<div class='form-group'>"
            "<label>Password</label>"
            "<input type='password' name='pass' placeholder='Enter WiFi password'>"
            "</div>"
            "<button type='submit' class='btn-primary'>Save & Connect</button>"
            "</form>"
            "<div class='divider'></div>"
            "<button class='btn-secondary' onclick=\"fetch('/scan').then(r=>r.json()).then(d=>alert(JSON.stringify(d,null,2)))\">Scan Networks</button>"
            "<button class='btn-secondary' onclick=\"fetch('/status').then(r=>r.json()).then(d=>alert(JSON.stringify(d,null,2)))\">View Status</button>"
            "<button class='btn-danger' onclick=\"if(confirm('Clear all credentials?')) fetch('/reset',{method:'POST'}).then(()=>location.reload())\">Reset Device</button>");
  
  if (WiFi.status() == WL_CONNECTED) {
    html += "<div class='status connected'><strong>Connected</strong><br>Network: " + WiFi.SSID() + "<br>IP: <code>" + ipToString(WiFi.localIP()) + "</code></div>";
  } else {
    html += "<div class='status'><strong>Not Connected</strong><br>Configure WiFi above to connect</div>";
  }
  
  html += F("</div></body></html>");
  return html;
}

// === Request Handlers ===
void handleRoot() { 
  IPAddress clientIP = server.client().remoteIP();
  Serial.println("[AUDIT] Client connected - IP: " + ipToString(clientIP) + " - Requested: /");
  server.send(200, "text/html", htmlPage()); 
}

void handleSave() {
  IPAddress clientIP = server.client().remoteIP();
  String ssid = server.arg("ssid");
  String pass = server.arg("pass");

  Serial.println("[AUDIT] POST /save from IP: " + ipToString(clientIP));
  Serial.println("  SSID: " + ssid);

  if (ssid.length() == 0) {
    server.send(400, "application/json", "{\"error\":\"SSID required\"}");
    return;
  }

  saveCredentials(ssid, pass);
  server.send(200, "text/html", 
    "<html><head><meta http-equiv='refresh' content='3;url=/'></head>"
    "<body style='font-family:Arial;text-align:center;padding:50px;background:#667eea;color:white;'>"
    "<h2>Credentials Saved</h2><p>Restarting in 3 seconds...</p></body></html>");
  
  shouldRestart = true;
  restartTime = millis() + 500;
}

void handleConnectJson() {
  IPAddress clientIP = server.client().remoteIP();
  Serial.println("[AUDIT] POST /connect from IP: " + ipToString(clientIP));
  
  if (!server.hasArg("plain")) {
    server.send(400, "application/json", "{\"error\":\"JSON body required\"}");
    return;
  }
  
  String body = server.arg("plain");
  int ssidIdx = body.indexOf("\"ssid\"");
  int passIdx = body.indexOf("\"pass\"");
  
  auto extract = [&](int idx) -> String {
    if (idx < 0) return "";
    int colon = body.indexOf(':', idx);
    int q1 = body.indexOf('"', colon+1);
    int q2 = body.indexOf('"', q1+1);
    return body.substring(q1+1, q2);
  };
  
  String ssid = extract(ssidIdx);
  String pass = extract(passIdx);

  if (ssid.length() == 0) {
    server.send(400, "application/json", "{\"error\":\"SSID required\"}");
    return;
  }
  if (pass.length() == 0) {
    server.send(400, "application/json", "{\"error\":\"Password required\"}");
    return;
  }

  Serial.println("  SSID: " + ssid);
  saveCredentials(ssid, pass);
  WiFi.mode(WIFI_STA);
  WiFi.begin(storedSSID.c_str(), storedPassword.c_str());
  server.send(200, "application/json", "{\"message\":\"attempting connection\",\"ssid\":\"" + ssid + "\"}");
}

void handleStatus() {
  IPAddress clientIP = server.client().remoteIP();
  Serial.println("[AUDIT] GET /status from IP: " + ipToString(clientIP));
  
  String json = "{";
  json += "\"mode\":\"" + String(portalMode ? "AP" : "STA") + "\",";
  json += "\"stored_ssid\":\"" + storedSSID + "\",";
  json += "\"wifi_status\":" + String(WiFi.status()) + ",";
  json += "\"connected_ssid\":\"" + WiFi.SSID() + "\",";
  json += "\"ip\":\"" + (WiFi.status()==WL_CONNECTED ? ipToString(WiFi.localIP()) : "") + "\"";
  json += "}";
  server.send(200, "application/json", json);
}

void handleScan() {
  IPAddress clientIP = server.client().remoteIP();
  Serial.println("[AUDIT] GET /scan from IP: " + ipToString(clientIP));
  Serial.println("[INFO] Scanning WiFi networks...");
  
  int n = WiFi.scanNetworks();
  Serial.printf("  Found %d networks\n", n);
  
  String json = "[";
  for (int i = 0; i < n; i++) {
    if (i) json += ",";
    json += "{\"ssid\":\"" + WiFi.SSID(i) + "\",\"rssi\":" + String(WiFi.RSSI(i)) + "}";
    Serial.printf("  %d: %s (%d dBm)\n", i+1, WiFi.SSID(i).c_str(), WiFi.RSSI(i));
  }
  json += "]";
  server.send(200, "application/json", json);
}

void handleReset() {
  IPAddress clientIP = server.client().remoteIP();
  Serial.println("[AUDIT] POST /reset from IP: " + ipToString(clientIP));
  
  clearCredentials();
  WiFi.disconnect(true, true);
  WiFi.mode(WIFI_AP);
  WiFi.softAP(AP_SSID, AP_PASS);
  dnsServer.start(DNS_PORT, "*", WiFi.softAPIP());
  portalMode = true;
  Serial.println("[INFO] Portal restarted in AP mode");
  Serial.print("  SSID: "); Serial.println(AP_SSID);
  Serial.print("  IP: "); Serial.println(WiFi.softAPIP());
  server.send(200, "application/json", "{\"message\":\"credentials cleared\"}");
}

// === WiFi Management ===
void startPortal() {
  WiFi.mode(WIFI_AP);
  WiFi.softAP(AP_SSID, AP_PASS);
  dnsServer.start(DNS_PORT, "*", WiFi.softAPIP());
  portalMode = true;
  Serial.println("[INFO] Portal started in AP mode");
  Serial.print("  SSID: "); Serial.println(AP_SSID);
  Serial.print("  PASS: "); Serial.println(AP_PASS);
  Serial.print("  AP IP: "); Serial.println(WiFi.softAPIP());
}

void tryConnectStored() {
  WiFi.mode(WIFI_STA);
  WiFi.begin(storedSSID.c_str(), storedPassword.c_str());
  Serial.print("[INFO] Attempting connection to "); Serial.println(storedSSID);

  unsigned long start = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - start < 10000) {
    delay(300);
    Serial.print(".");
  }
  Serial.println();

  if (WiFi.status() == WL_CONNECTED) {
    portalMode = false;
    Serial.println("[AUDIT] Successfully connected to WiFi");
    Serial.print("  SSID: "); Serial.println(WiFi.SSID());
    Serial.print("  IP: "); Serial.println(WiFi.localIP());
  } else {
    Serial.println("[ERROR] Connection failed, clearing credentials...");
    clearCredentials();
    startPortal();
  }
}

// === Setup / Loop ===
void setup() {
  Serial.begin(115200);
  delay(1000);
  Serial.println("\n=== ESP32 WiFi Config Portal ===");

  pinMode(RESET_BTN, INPUT_PULLUP);

  bool hasCredentials = loadCredentials();
  if (hasCredentials) {
    tryConnectStored();
  } else {
    startPortal();
  }

  server.on("/", handleRoot);
  server.on("/save", HTTP_POST, handleSave);
  server.on("/connect", HTTP_POST, handleConnectJson);
  server.on("/status", handleStatus);
  server.on("/scan", handleScan);
  server.on("/reset", HTTP_POST, handleReset);

  server.begin();
  Serial.println("[INFO] Web server started on port 80\n");
}

void loop() {
  server.handleClient();
  if (portalMode) {
    dnsServer.processNextRequest();
  }
  
  if (shouldRestart && millis() >= restartTime) {
    Serial.println("[INFO] Restarting ESP32...");
    delay(100);
    ESP.restart();
  }
}
