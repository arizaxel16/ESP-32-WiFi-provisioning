// esp32_ap_config.ino
#include <WiFi.h>
#include <WebServer.h>
#include <DNSServer.h>
#include <Preferences.h>
#include <ArduinoJson.h>

// ===== Globals =====
WebServer server(80);
DNSServer dnsServer;
Preferences preferences;

const byte DNS_PORT = 53;

// Default AP credentials (used for configuration portal)
const char* ap_ssid = "ESP32_Config_AP";
const char* ap_pass = "config123";

// Captive portal HTML (simple form)
const char* captivePage = R"rawliteral(
<!DOCTYPE html><html><head><meta charset="utf-8"><meta name="viewport" content="width=device-width, initial-scale=1"></head>
<body>
  <h3>Configura la WiFi</h3>
  <form method="POST" action="/save">
    <label>SSID:</label><br>
    <input name="ssid" required><br>
    <label>Password:</label><br>
    <input name="password" type="password"><br>
    <button type="submit">Guardar y conectar</button>
  </form>
</body></html>
)rawliteral";

// Reconnect attempt timing
unsigned long lastAttempt = 0;
const unsigned long attemptInterval = 10000; // 10s

// ===== Helper functions =====
bool hasSavedCredentials() {
  preferences.begin("wifi", true); // readonly
  String s = preferences.getString("ssid", "");
  preferences.end();
  return s.length() > 0;
}

bool connectToSavedNetwork(unsigned long timeoutMs = 15000) {
  preferences.begin("wifi", true);
  String savedSsid = preferences.getString("ssid", "");
  String savedPass = preferences.getString("pass", "");
  preferences.end();

  if (savedSsid.length() == 0) return false;

  Serial.printf("Intentando conectar a SSID: %s\n", savedSsid.c_str());
  WiFi.mode(WIFI_STA);
  WiFi.begin(savedSsid.c_str(), savedPass.c_str());

  unsigned long start = millis();
  while (millis() - start < timeoutMs) {
    if (WiFi.status() == WL_CONNECTED) {
      Serial.println("Conectado a la red guardada.");
      Serial.print("IP: ");
      Serial.println(WiFi.localIP());
      return true;
    }
    delay(200);
  }

  Serial.println("No se pudo conectar a la red guardada (timeout)." );
  return false;
}

// ===== Handlers =====
void handleStatus() {
  DynamicJsonDocument doc(512);
  bool connected = (WiFi.status() == WL_CONNECTED);
  doc["mode"] = (connected ? "STA" : "AP");
  doc["ip"] = (connected ? WiFi.localIP().toString() : WiFi.softAPIP().toString());
  doc["connected"] = connected;

  preferences.begin("wifi", true);
  String s = preferences.getString("ssid", "");
  preferences.end();
  if (s.length()) {
    String masked = s;
    if (masked.length() > 2) masked = masked.substring(0,2) + String("****");
    doc["saved_ssid"] = masked;
  } else {
    doc["saved_ssid"] = "";
  }

  String out;
  serializeJson(doc, out);
  server.send(200, "application/json", out);
}

void handleWiFiScan() {
  int n = WiFi.scanNetworks(true, true); // asynchronous scan: start and return results later (some cores)
  // Note: depending on core support, you might need a blocking scan using WiFi.scanNetworks()
  // For compatibility we'll block here:
  if (n == -1) {
    n = WiFi.scanNetworks(); // fallback blocking scan
  }
  DynamicJsonDocument doc(2048);
  JsonArray arr = doc.createNestedArray("networks");
  for (int i=0;i<n;i++) {
    JsonObject item = arr.createNestedObject();
    item["ssid"] = WiFi.SSID(i);
    item["rssi"] = WiFi.RSSI(i);
    item["enc"] = WiFi.encryptionType(i);
  }
  String out;
  serializeJson(doc, out);
  server.send(200, "application/json", out);
}

void handleReset() {
  Preferences pref;
  pref.begin("wifi", false);
  pref.clear();
  pref.end();
  server.send(200, "application/json", "{\"result\":\"credentials_cleared\"}");
  delay(500);
  ESP.restart();
}

// ===== Server start helpers =====
void startServerForSTA() {
  // Clear any previous routes and register STA routes
  server.reset();
  server.on("/", handleStatus);
  server.on("/api/status", handleStatus);
  server.on("/api/wifi-scan", handleWiFiScan);
  server.on("/api/reset", HTTP_POST, handleReset);
  server.begin();
  Serial.println("Servidor HTTP en modo STA iniciado.");
}

void startCaptivePortal() {
  WiFi.mode(WIFI_AP);
  WiFi.softAP(ap_ssid, ap_pass);
  delay(500);
  IPAddress apIP = WiFi.softAPIP();
  dnsServer.start(DNS_PORT, "*", apIP);

  // Register portal routes
  server.reset();
  server.on("/", [](){ server.send(200, "text/html", captivePage); });

  server.on("/save", HTTP_POST, [](){
    if (server.hasArg("ssid")) {
      String s = server.arg("ssid");
      String p = server.arg("password");

      Preferences pref;
      pref.begin("wifi", false);
      pref.putString("ssid", s);
      pref.putString("pass", p);
      pref.end();

      server.send(200, "application/json", "{\"result\":\"saved\"}");

      delay(500);
      ESP.restart(); // restart to attempt STA connection
    } else {
      server.send(400, "application/json", "{\"error\":\"missing fields\"}");
    }
  });

  server.onNotFound([](){
    // In captive portal mode redirect everything to the portal root
    server.sendHeader("Location", String("http://") + WiFi.softAPIP().toString(), true);
    server.send(302, "text/plain", "");
  });

  server.begin();
  Serial.print("Captive portal iniciado. AP IP: ");
  Serial.println(apIP);
}

// ===== Setup & Loop =====
void setup() {
  Serial.begin(115200);
  delay(1000);

  Serial.println("=== ESP32 WiFi Config Starting ===");

  // If saved credentials exist, try to connect as STA
  if (hasSavedCredentials()) {
    if (connectToSavedNetwork(10000)) {
      // Connected in STA mode -> start API server for STA mode
      startServerForSTA();
      return;
    } else {
      Serial.println("Fallo al conectar con credenciales guardadas. Iniciando portal configuracion.");
    }
  } else {
    Serial.println("No hay credenciales guardadas. Iniciando portal configuracion.");
  }

  // If we reach here: no credentials or connection failed -> start captive portal
  startCaptivePortal();
}

void loop() {
  server.handleClient();
  // If in AP mode, process DNS requests (required for captive portal behavior)
  if (WiFi.getMode() == WIFI_AP) {
    dnsServer.processNextRequest();
  }

  // If we have saved credentials but are not connected, attempt reconnect every interval
  if (!WiFi.isConnected() && hasSavedCredentials()) {
    if (millis() - lastAttempt > attemptInterval) {
      lastAttempt = millis();
      Serial.println("Intento de reconexión a credenciales guardadas...");
      if (connectToSavedNetwork(10000)) {
        // Switch server to STA mode routes
        startServerForSTA();
      }
    }
  }
}
