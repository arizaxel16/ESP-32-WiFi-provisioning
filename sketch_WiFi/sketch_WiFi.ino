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

void loop() {
  // Maneja las solicitudes entrantes
  server.handleClient();
}
