#include <HardwareSerial.h>
#include <WiFi.h>
#include <PubSubClient.h>

HardwareSerial lelitSerial(2);

// WiFi credentials
const char* ssid = "WIFI_USERNAME";
const char* password = "WIFI_PASSWORD";

// MQTT Broker settings
const char* mqtt_broker = "homeassistant.local";
const char* mqtt_topic = "lelit/temperature";
const char* mqtt_username = "MQTT-HOMEASSISTANT-USER-USERNAME";
const char* mqtt_password = "MQTT-HOMEASSISTANT-USER-PASSWORD";
const int mqtt_port = 1883;

WiFiClient espClient;
PubSubClient mqttClient(espClient);

const int FRAME_LEN = 8;
uint8_t frame[FRAME_LEN];
int pos = 0;
bool synced = false;

// ==== Median Filter with Outlier Rejection ====
#define WINDOW_SIZE 12
int values[WINDOW_SIZE];
int indexMedian = 0;
bool bufferFilled = false;
int lastValidTemp = -1;

int medianFilter(int newValue) {
  // Step 1: Reject obvious outliers BEFORE adding to filter
  // Temperature range check: 32-250°F (0-120°C)
  if (newValue < 32 || newValue > 250) {
    Serial.print("Outlier rejected (range): ");
    Serial.println(newValue);
    if (lastValidTemp != -1) {
      return lastValidTemp;
    }
    return newValue; // First reading, accept it
  }
  
  // Step 2: Rate-of-change check
  // Reject values that change more than 30°F (~17°C) from last valid value
  if (lastValidTemp != -1 && abs(newValue - lastValidTemp) > 30) {
    Serial.print("Outlier rejected (spike): ");
    Serial.print(newValue);
    Serial.print(" (last valid: ");
    Serial.print(lastValidTemp);
    Serial.println(")");
    return lastValidTemp; // Return last known good value
  }
  
  // Step 3: Add to median buffer (only if passed outlier checks)
  values[indexMedian] = newValue;
  indexMedian = (indexMedian + 1) % WINDOW_SIZE;
  if (indexMedian == 0) bufferFilled = true;

  // Step 4: Calculate median
  int n = bufferFilled ? WINDOW_SIZE : indexMedian;
  if (n == 0) return newValue;
  
  int sorted[n];
  memcpy(sorted, values, n * sizeof(int));

  // Bubble sort
  for (int i = 0; i < n - 1; i++) {
    for (int j = i + 1; j < n; j++) {
      if (sorted[j] < sorted[i]) {
        int tmp = sorted[i];
        sorted[i] = sorted[j];
        sorted[j] = tmp;
      }
    }
  }
  
  int median = sorted[n / 2];
  lastValidTemp = median; // Update last valid temperature
  return median;
}

void connectWiFi() {
  Serial.print("Connecting to WiFi");
  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("\nWiFi connected");
  Serial.print("IP address: ");
  Serial.println(WiFi.localIP());
}

void connectMQTT() {
  while (!mqttClient.connected()) {
    String client_id = "esp32-lelit-";
    client_id += String(WiFi.macAddress());
    Serial.print("Connecting to MQTT broker...");

    if (mqttClient.connect(client_id.c_str(), mqtt_username, mqtt_password)) {
      Serial.println("connected");
    } else {
      Serial.print("failed, rc=");
      Serial.print(mqttClient.state());
      Serial.println(" retrying in 2 seconds");
      delay(2000);
    }
  }
}

void setup() {
  btStop();
  delay(1000);
  Serial.begin(115200);
  lelitSerial.begin(9600, SERIAL_8N1, 16, 17);

  Serial.println(F("Sniffing Lelit with 0x3F start byte"));

  connectWiFi();
  mqttClient.setServer(mqtt_broker, mqtt_port);
}

void loop() {
  if (!mqttClient.connected()) {
    connectMQTT();
  }
  mqttClient.loop();

  while (lelitSerial.available()) {
    uint8_t b = lelitSerial.read();

    if (!synced) {
      if (b == 0x3F) {
        frame[0] = b;
        pos = 1;
        synced = true;
      }
      continue;
    }

    frame[pos++] = b;

    if (pos == FRAME_LEN) {
      synced = false;
      pos = 0;

      // Parse temperature from frame
      uint8_t tempPreread = ((uint8_t)(~frame[2])) >> 1;
      int tempF = (frame[3] == 0xFF) ? tempPreread : (tempPreread + 128);

      // Debug: Print raw value
      Serial.print("Raw temp: ");
      Serial.print(tempF);
      Serial.print(" F -> ");

      // Apply median filter with outlier rejection
      int filteredTempF = medianFilter(tempF);
      int tempC = (int)((filteredTempF - 32.0) / 1.8);

      Serial.print("Filtered: ");
      Serial.print(tempC);
      Serial.println(" C");

      // Publish to MQTT with rate limiting
      static int lastTemp = -999;
      static unsigned long lastPublish = 0;
      const unsigned long publishInterval = 5000;  // 5s
      if (tempC != lastTemp || millis() - lastPublish > publishInterval) {
        lastTemp = tempC;
        lastPublish = millis();
        mqttClient.publish(mqtt_topic, String(lastTemp).c_str());
        Serial.println("Published to MQTT");
      }
    }
  }
}
