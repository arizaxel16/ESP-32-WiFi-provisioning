// esp32_ap_config_no_reset.ino
#include <WiFi.h>
#include <WebServer.h>
#include <DNSServer.h>
#include <Preferences.h>

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

// ===== Helpers =====
// Escape " and \ for safe JSON strings
String jsonEscape(const String &s) {
  String out;
  out.reserve(s.length() + 8);
  for (size_t i = 0; i < s.length(); ++i) {
    char c = s[i];
    if (c == '\\' || c == '"') {
      out += '\\';
      out += c;
    } else if (c == '\n') {
      out += "\\n";
    } else if (c == '\r') {
      out += "\\r";
    } else {
      out += c;
    }
  }
  return out;
}

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
      // Ensure DNS server stops when we become STA
      dnsServer.stop();
      return true;
    }
    delay(200);
  }

  Serial.println("No se pudo conectar a la red guardada (timeout)." );
  return false;
}

// ===== Handlers =====
void handleStatus() {
  bool connected = (WiFi.status() == WL_CONNECTED);
  String ip = (connected ? WiFi.localIP().toString() : WiFi.softAPIP().toString());

  preferences.begin("wifi", true);
  String s = preferences.getString("ssid", "");
  preferences.end();

  String masked = "";
  if (s.length()) {
    masked = s;
    if (masked.length() > 2) masked = masked.substring(0,2) + String("****");
    masked = jsonEscape(masked);
  }

  // Build JSON manually
  String out;
  out.reserve(200);
  out += "{";
  out += "\"mode\":\"";
  out += (connected ? "STA" : "AP");
  out += "\",";
  out += "\"ip\":\"";
  out += jsonEscape(ip);
  out += "\",";
  out += "\"connected\":";
  out += (connected ? "true" : "false");
  out += ",";
  out += "\"saved_ssid\":\"";
  out += masked;
  out += "\"";
  out += "}";

  server.send(200, "application/json", out);
}

void handleWiFiScan() {
  int n = WiFi.scanNetworks(); // blocking scan
  if (n < 0) n = 0;

  // Build JSON array
  String out;
  out.reserve(1024);
  out += "{\"networks\":[";

  for (int i = 0; i < n; ++i) {
    String ssid = jsonEscape(WiFi.SSID(i));
    int rssi = WiFi.RSSI(i);
    int enc = WiFi.encryptionType(i);

    out += "{";
    out += "\"ssid\":\""; out += ssid; out += "\",";
    out += "\"rssi\":"; out += String(rssi); out += ",";
    out += "\"enc\":"; out += String(enc);
    out += "}";
    if (i < n - 1) out += ",";
  }

  out += "]}";

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

// This root handler behaves as portal root if in AP mode, otherwise shows status JSON
void handleRoot() {
  if (WiFi.getMode() == WIFI_AP) {
    server.send(200, "text/html", captivePage);
  } else {
    handleStatus();
  }
}

// Save handler: only valid when in AP mode (portal). Otherwise returns 404-like JSON.
void handleSave() {
  if (WiFi.getMode() != WIFI_AP) {
    server.send(400, "application/json", "{\"error\":\"not in AP mode\"}");
    return;
  }
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
}

// NotFound: when in AP redirect everything to portal root; else return default 404
void handleNotFound() {
  if (WiFi.getMode() == WIFI_AP) {
    server.sendHeader("Location", String("http://") + WiFi.softAPIP().toString(), true);
    server.send(302, "text/plain", "");
  } else {
    server.send(404, "text/plain", "Not Found");
  }
}

// ===== Setup & Loop =====
void startAPAndCaptivePortal() {
  WiFi.mode(WIFI_AP);
  WiFi.softAP(ap_ssid, ap_pass);
  delay(500);
  IPAddress apIP = WiFi.softAPIP();
  dnsServer.start(DNS_PORT, "*", apIP);
  Serial.print("Captive portal AP started. IP: ");
  Serial.println(apIP);
}

void setup() {
  Serial.begin(115200);
  delay(1000);

  Serial.println("=== ESP32 WiFi Config Starting ===");

  // Register routes once
  server.on("/", handleRoot);
  server.on("/save", HTTP_POST, handleSave);
  server.on("/api/status", HTTP_GET, handleStatus);
  server.on("/api/wifi-scan", HTTP_GET, handleWiFiScan);
  server.on("/api/reset", HTTP_POST, handleReset);
  server.onNotFound(handleNotFound);

  server.begin();

  // Boot decision: try STA if saved credentials, else AP portal
  if (hasSavedCredentials()) {
    if (connectToSavedNetwork(10000)) {
      Serial.println("Booted in STA mode; API available.");
      // DNS server already stopped inside connectToSavedNetwork() on success
      return;
    } else {
      Serial.println("Fallo al conectar con credenciales guardadas. Iniciando portal configuracion.");
    }
  } else {
    Serial.println("No hay credenciales guardadas. Iniciando portal configuracion.");
  }

  startAPAndCaptivePortal();
}

void loop() {
  server.handleClient();
  if (WiFi.getMode() == WIFI_AP) {
    dnsServer.processNextRequest();
  }

  // If we have saved credentials but are not connected, attempt reconnect every interval
  if (!WiFi.isConnected() && hasSavedCredentials()) {
    if (millis() - lastAttempt > attemptInterval) {
      lastAttempt = millis();
      Serial.println("Intento de reconexión a credenciales guardadas...");
      if (connectToSavedNetwork(10000)) {
        Serial.println("Reconectado en loop; API disponible en STA mode.");
        // If we were an AP previously, stop DNS server to clean up captive behavior
        dnsServer.stop();
      }
    }
  }
}
