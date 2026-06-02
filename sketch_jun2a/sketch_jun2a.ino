#include <Arduino.h>
#include <WiFi.h>
#include <WiFiUdp.h>
#include <OSCMessage.h>

const char *WIFI_SSID = "MUSIC-TIM";
const char *WIFI_PASSWORD = "***REMOVED***";

// OSC & UDP Settings
WiFiUDP Udp;
const IPAddress outIp(192, 168, 137, 1);
const unsigned int outPort = 8000;
const unsigned int localPort = 8888;

void connectWiFi() {
  Serial.println("\n--- ESP32-S2 WiFi Connection ---");

  WiFi.disconnect(true, true);
  delay(1000);

  WiFi.mode(WIFI_STA);

  WiFi.setSleep(false);

  Serial.printf("Attempting link to SSID: %s\n", WIFI_SSID);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

  int attemptCounter = 0;

  while (WiFi.status() != WL_CONNECTED) {
    delay(1000);
    attemptCounter++;

    wl_status_t status = WiFi.status();
    Serial.printf("[Attempt %02d] Status Code: %d -> ", attemptCounter, status);

    switch (status) {
    case WL_NO_SSID_AVAIL:
      Serial.println(
          "Error: SSID not found. Is the hotspot hidden or too far?");
      break;
    case WL_CONNECT_FAILED:
      Serial.println(
          "Error: Connection failed. Double check your password keys.");
      break;
    case WL_CONNECTION_LOST:
      Serial.println("Error: Connection lost.");
      break;
    case WL_DISCONNECTED:
      Serial.println("Searching / Handshaking...");
      break;
    case WL_IDLE_STATUS:
      Serial.println("Idle state...");
      break;
    default:
      Serial.println("Processing...");
      break;
    }

    if (attemptCounter >= 20) {
      Serial.println(
          "\n[Timeout] Resetting WiFi radio and trying once more...");
      WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
      attemptCounter = 0;
    }
  }

  Serial.println("\n=========================================");
  Serial.println("SUCCESS: CONNECTED");
  Serial.print("ESP32 IP Address: ");
  Serial.println(WiFi.localIP());
  Serial.print("Signal Strength (RSSI): ");
  Serial.print(WiFi.RSSI());
  Serial.println(" dBm");
  Serial.println("=========================================");
}

void setup() {
  Serial.begin(115200);

  for (int i = 3; i > 0; i--) {
    Serial.printf("Starting diagnostics in %d...\n", i);
    delay(1000);
  }

  connectWiFi();

  // Start UDP for OSC
  Udp.begin(localPort);
  Serial.println("UDP initialized for OSC communication.");
}

void loop() {
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("Connection dropped! Reconnecting...");
    connectWiFi();
  } else {
    Serial.println("Connection stable... sending OSC test message.");

    OSCMessage msg("/esp32/test");
    msg.add("hello from ESP32!");
    msg.add((int32_t)millis());

    Udp.beginPacket(outIp, outPort);
    msg.send(Udp);
    Udp.endPacket();
    msg.empty();

    // delay(2000);
  }
}