#include <ESP8266WiFi.h>

void setup() {
  Serial.begin(115200);
  WiFi.mode(WIFI_AP);
  WiFi.softAP("Wind_Sensor", "12345678");
  Serial.println("AP started");
  Serial.println("IP: 192.168.4.1");
}

void loop() {
  delay(1000);
  Serial.println("running...");
}
