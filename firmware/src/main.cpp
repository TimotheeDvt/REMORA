#include <Arduino.h>
#include <WiFi.h>
#include <WiFiUdp.h>
#include <Wire.h>
#include <OSCMessage.h>
#include "MPU9250.h"
#include "GatedMahony.h"
#include "secrets.h" // git-ignored; copy include/secrets.example.h to create it

// Network Configuration
const char *WIFI_SSID = WIFI_SSID_VALUE;
const char *WIFI_PASSWORD = WIFI_PASSWORD_VALUE;

WiFiUDP Udp;
const IPAddress outIp(192, 168, 12, 1);
const uint16_t outPort = 8000;
const uint16_t localPort = 8888;

MPU9250 mpu;
GatedMahony orientation;
uint32_t packetSequence = 0;

#define SEND_INTERVAL_MS 10 // 100 Hz transmission rate
#define LED_PIN 13          // Blue STAT LED

// WiFi
void connectWiFi() {
  Serial.println("\n--- ESP32-S2 WiFi Connection ---");
  WiFi.mode(WIFI_STA);
  WiFi.setSleep(false);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

  Serial.printf("Connecting to: %s\n", WIFI_SSID);

  uint8_t attempts = 0;
  bool ledState = false;
  while (WiFi.status() != WL_CONNECTED) {
    ledState = !ledState;
    digitalWrite(LED_PIN, ledState); // Blink while not connected
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

  digitalWrite(LED_PIN, HIGH);

  Serial.println("\n=========================================");
  Serial.println("SUCCESS: CONNECTED");
  Serial.print("IP   : ");
  Serial.println(WiFi.localIP());
  Serial.println("=========================================\n");
}

// Averages the gyro over ~1 s at boot. The staff must lie still; if it moves, the bias is left at
// zero and GatedMahony's still-period tracking takes over once the staff is put down.
void calibrateGyroBias() {
  constexpr int kSamples = 200;
  constexpr float kMaxSpreadDegPerSec = 5.0f;
  float sum[3] = {0, 0, 0}, lo[3] = {1e9f, 1e9f, 1e9f}, hi[3] = {-1e9f, -1e9f, -1e9f};
  int n = 0;
  const uint32_t tStart = millis();
  while (n < kSamples && millis() - tStart < 3000) {
    if (!mpu.update()) continue;
    const float g[3] = {mpu.getGyroX(), mpu.getGyroY(), mpu.getGyroZ()};
    for (int i = 0; i < 3; ++i) {
      sum[i] += g[i];
      lo[i] = min(lo[i], g[i]);
      hi[i] = max(hi[i], g[i]);
    }
    ++n;
  }
  if (n == 0) return;
  for (int i = 0; i < 3; ++i)
    if (hi[i] - lo[i] > kMaxSpreadDegPerSec) {
      Serial.println("Gyro bias: staff moved during boot, using still-period tracking only");
      return;
    }
  orientation.setGyroBias(sum[0] / n, sum[1] / n, sum[2] / n);
  Serial.printf("Gyro bias (deg/s): %.2f %.2f %.2f\n", sum[0] / n, sum[1] / n, sum[2] / n);
}

void setup() {
  Serial.begin(115200);

  uint32_t tStart = millis();
  while (!Serial && (millis() - tStart < 5000)) delay(10);

  Serial.println("\n\n=========================================");
  Serial.println("ESP32-S2 IS BOOTING... (MPU-9250 version)");
  Serial.println("=========================================\n");

  pinMode(LED_PIN, OUTPUT);
  digitalWrite(LED_PIN, LOW); // Start with LED off

  Wire.begin(1, 2);
  Wire.setClock(400000);
  Wire.setTimeOut(20);

  Serial.print("Initializing MPU-9250... ");
  mpu.setup(0x68);
  // Orientation comes from GatedMahony (6-axis: accel + gyro, magnetometer ignored, so it is not
  // pulled around by the staff's electronics or stage magnetic interference). The library's own
  // filter is switched off; it is only used to read the sensor.
  mpu.selectFilter(QuatFilterSel::NONE);
  Serial.println("OK");

  calibrateGyroBias();

  connectWiFi();

  Udp.begin(localPort);

  String ipStr = WiFi.localIP().toString();
  OSCMessage msg("/esp32/connected");
  msg.add(ipStr.c_str());
  Udp.beginPacket(outIp, outPort);
  msg.send(Udp);
  Udp.endPacket();

  Serial.println("\nSetup complete - streaming at up to 100 Hz");
}

void loop() {
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("WiFi lost - reconnecting...");
    connectWiFi();
    return;
  }

  if (mpu.update()) {
    static uint32_t lastSampleMicros = micros();
    static uint32_t lastSend = 0;
    const uint32_t nowMicros = micros();

    // Get Accel/Gyro/Mag values (floats)
    float ax = mpu.getAccX();
    float ay = mpu.getAccY();
    float az = mpu.getAccZ();

    float gx = mpu.getGyroX();
    float gy = mpu.getGyroY();
    float gz = mpu.getGyroZ();

    // Fuse every sample, not only the ones we send.
    orientation.update(ax, ay, az, gx, gy, gz, (nowMicros - lastSampleMicros) * 1e-6f);
    lastSampleMicros = nowMicros;

    const uint32_t now = millis();
    // Only transmit at the desired interval (100 Hz)
    if (now - lastSend >= SEND_INTERVAL_MS) {
      lastSend = now;

    float mx = mpu.getMagX();
    float my = mpu.getMagY();
    float mz = mpu.getMagZ();

    // Build & Send OSC. Gyro is sent with the tracked bias removed. The sequence number lets the
    // plugin detect lost or reordered packets; the timestamp (sensor clock, us) gives it the true
    // time between samples instead of the Wi-Fi arrival time.
    OSCMessage msg("/esp32/imu");
    msg.add(ax).add(ay).add(az);
    msg.add(gx - orientation.biasX()).add(gy - orientation.biasY()).add(gz - orientation.biasZ());
    msg.add(mx).add(my).add(mz);
    msg.add(orientation.w()).add(orientation.x()).add(orientation.y()).add(orientation.z());
    msg.add(WiFi.localIP().toString().c_str());
    msg.add((intOSC_t)packetSequence++);
    msg.add((intOSC_t)nowMicros);

    Udp.beginPacket(outIp, outPort);
    msg.send(Udp);
    Udp.endPacket();
    }
  }
}
