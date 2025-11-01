#include <HardwareSerial.h>
HardwareSerial lelitSerial(8, 9);

const int FRAME_LEN = 8;
uint8_t frame[FRAME_LEN];
int pos = 0;
bool synced = false;

// ==== Median Filter ====
#define WINDOW_SIZE 15
int values[WINDOW_SIZE];
int indexMedian = 0;
bool bufferFilled = false;

int medianFilter(int newValue) {
  values[indexMedian] = newValue;
  indexMedian = (indexMedian + 1) % WINDOW_SIZE;
  if (indexMedian == 0) bufferFilled = true;

  int n = bufferFilled ? WINDOW_SIZE : indexMedian;
  int sorted[n];
  memcpy(sorted, values, n * sizeof(int));

  // Simple bubble sort (fast enough for n=15)
  for (int i = 0; i < n - 1; i++) {
    for (int j = i + 1; j < n; j++) {
      if (sorted[j] < sorted[i]) {
        int tmp = sorted[i];
        sorted[i] = sorted[j];
        sorted[j] = tmp;
      }
    }
  }

  return sorted[n / 2];
}
// =======================

void printBinary8(byte val) {
  for (int i = 7; i >= 0; i--) {
    Serial.print(bitRead(val, i));
  }
}

void setup() {
  Serial.begin(115200);
  lelitSerial.begin(9600);
  Serial.println(F("Sniffing Lelit with 0x3F start byte"));
}

void loop() {
  while (lelitSerial.available()) {
    uint8_t b = lelitSerial.read();

    if (!synced) {
      if (b == 0x3F) {  // start byte
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

      uint8_t tempPreread = ((uint8_t)(~frame[2])) >> 1;
      unsigned int tempF = (frame[3] == 0xFF) ? tempPreread : (tempPreread + 128);

      // ---- Median filter ----
      int filteredTempF = medianFilter(tempF);

      // ---- Output in °C ----
      int tempC = (int)((filteredTempF - 32) / 1.8);
      Serial.println(tempC);
    }
  }
}
