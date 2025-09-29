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

void loop() {
  // Maneja las solicitudes entrantes
  server.handleClient();
}
