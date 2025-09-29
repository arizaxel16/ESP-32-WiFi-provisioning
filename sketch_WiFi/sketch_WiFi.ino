#include <WiFi.h>
#include <WebServer.h>

// Nombre y clave de la red creada por la ESP32
const char* ssid = "ESP32_AP";
const char* password = "12345678";

// Crear servidor HTTP en el puerto 80
WebServer server(80);

void handleRoot() {
  server.send(200, "text/plain", "Hello World from ESP32");
}

void setup() {
  Serial.begin(115200);
  delay(1000);

  Serial.println("Iniciando Access Point...");
  WiFi.softAP(ssid, password);

  Serial.print("Red creada con SSID: ");
  Serial.println(ssid);
  Serial.print("IP del Access Point: ");
  Serial.println(WiFi.softAPIP());

  // Definir ruta principal
  server.on("/", handleRoot);

  // Iniciar servidor
  server.begin();
  Serial.println("Servidor HTTP iniciado.");
}

#include <Preferences.h>

Preferences preferences;

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
    delay(500);
  }
  Serial.println("No se pudo conectar a la red guardada (timeout)." );
  return false;
}

#include <DNSServer.h>

DNSServer dnsServer;
const byte DNS_PORT = 53;

const char* ap_ssid = "ESP32_Config_AP";
const char* ap_pass = "config123";

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

void startCaptivePortal() {
  WiFi.mode(WIFI_AP);
  WiFi.softAP(ap_ssid, ap_pass);

  delay(500);
  IPAddress apIP = WiFi.softAPIP();
  dnsServer.start(DNS_PORT, "*", apIP);

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
      ESP.restart(); // reiniciar para intentar conectar en modo STA
    } else {
      server.send(400, "application/json", "{\"error\":\"missing fields\"}");
    }
  });

  server.onNotFound([](){
    // Redirigir todas las peticiones a la página principal (comportamiento de portal cautivo)
    server.sendHeader("Location", String("http://") + WiFi.softAPIP().toString(), true);
    server.send(302, "text/plain", "");
  });

  server.begin();
  Serial.println("Captive portal iniciado.");
}

#include <ArduinoJson.h>

void handleStatus() {
  DynamicJsonDocument doc(256);
  bool connected = (WiFi.status() == WL_CONNECTED);
  doc["mode"] = (connected ? "STA" : "AP");
  doc["ip"] = (connected ? WiFi.localIP().toString() : WiFi.softAPIP().toString());

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
  int n = WiFi.scanNetworks();
  DynamicJsonDocument doc(1024);
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

unsigned long lastAttempt = 0;
const unsigned long attemptInterval = 10000; // 10s

void loop() {
  server.handleClient();
  if (WiFi.getMode() == WIFI_AP) {
    dnsServer.processNextRequest();
  }

  // Si tenemos credenciales guardadas pero no conectados, intentar cada X segundos
  if (!WiFi.isConnected() && hasSavedCredentials()) {
    if (millis() - lastAttempt > attemptInterval) {
      lastAttempt = millis();
      Serial.println("Intento de reconexión a credenciales guardadas...");
      if (connectToSavedNetwork(10000)) {
        // reiniciar servidor en modo STA (si es necesario reconfigurar)
        server.reset();
        server.on("/", handleStatus);
        server.on("/api/status", handleStatus);
        server.on("/api/wifi-scan", handleWiFiScan);
        server.on("/api/reset", HTTP_POST, handleReset);
        server.begin();
      }
    }
  }
}
