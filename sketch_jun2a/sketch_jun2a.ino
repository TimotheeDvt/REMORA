#include <Arduino.h>
#include <WiFi.h>
#include <WiFiUdp.h>
#include <Wire.h>
#include <OSCMessage.h>
#include "MPU9250.h"

// Network Configuration
const char *WIFI_SSID = "MUSIC-TIM";
const char *WIFI_PASSWORD = "***REMOVED***";

WiFiUDP Udp;
const IPAddress outIp(192, 168, 137, 1);
const uint16_t outPort = 8000;
const uint16_t localPort = 8888;

MPU9250 mpu;

#define SEND_INTERVAL_MS 20 // 50 Hz transmission rate

// WiFi
void connectWiFi() {
  Serial.println("\n--- ESP32-S2 WiFi Connection ---");
  WiFi.mode(WIFI_STA);
  WiFi.setSleep(false);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

  Serial.printf("Connecting to: %s\n", WIFI_SSID);

  uint8_t attempts = 0;
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print('.');
    if (++attempts >= 40) {
      Serial.println("\n[Timeout] Retrying...");
      WiFi.disconnect(true);
      delay(1000);
      WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
      attempts = 0;
    }
  }

  Serial.println("\n=========================================");
  Serial.println("SUCCESS: CONNECTED");
  Serial.print("IP   : ");
  Serial.println(WiFi.localIP());
  Serial.println("=========================================\n");
}

void setup() {
  Serial.begin(115200);

  uint32_t tStart = millis();
  while (!Serial && (millis() - tStart < 5000)) delay(10);

  Serial.println("\n\n=========================================");
  Serial.println("ESP32-S2 IS BOOTING... (MPU-9250 version)");
  Serial.println("=========================================\n");

  Wire.begin(1, 2);
  Wire.setClock(400000);
  Wire.setTimeOut(20);

  Serial.print("Initializing MPU-9250... ");
  mpu.setup(0x68)
  Serial.println("OK");

  connectWiFi();

  Udp.begin(localPort);

  String ipStr = WiFi.localIP().toString();
  OSCMessage msg("/esp32/connected");
  msg.add(ipStr.c_str());
  Udp.beginPacket(outIp, outPort);
  msg.send(Udp);
  Udp.endPacket();

  Serial.println("\nSetup complete - streaming at 50 Hz");
}

void loop() {
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("WiFi lost - reconnecting...");
    connectWiFi();
    return;
  }

  static uint32_t lastUpdate = 0;
  const uint32_t now = millis();

  if (now - lastUpdate < SEND_INTERVAL_MS) return;
  lastUpdate = now;

  if (mpu.update()) {
    // Get Accel/Gyro/Mag values (floats)
    float ax = mpu.getAccX();
    float ay = mpu.getAccY();
    float az = mpu.getAccZ();

    float gx = mpu.getGyroX();
    float gy = mpu.getGyroY();
    float gz = mpu.getGyroZ();

    float mx = mpu.getMagX();
    float my = mpu.getMagY();
    float mz = mpu.getMagZ();

    // Build & Send OSC
    OSCMessage msg("/esp32/imu");
    msg.add(ax).add(ay).add(az);
    msg.add(gx).add(gy).add(gz);
    msg.add(mx).add(my).add(mz);

    Udp.beginPacket(outIp, outPort);
    msg.send(Udp);
    Udp.endPacket();
  }
}