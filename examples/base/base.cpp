#include <Arduino.h>
#include <SafeWrap.h>
#include <algorithm>
#include <array>
#include <atomic>

struct MyData {
  uint8_t data1;
  uint16_t data2;
  float data3;
};

struct Sensor {
  uint8_t id;
  float temperature;
  float humidity;
  bool isActive;

  Sensor(uint8_t _id = 0, float _temp = 0.0f, float _hum = 0.0f, bool _active = false)
      : id(_id)
      , temperature(_temp)
      , humidity(_hum)
      , isActive(_active) {}
};

SafeWrap<MyData> myDataSafe(MyData{0, 0, 0});

SafeWrap<std::array<uint8_t, 10>> myArraySafe(
  std::array<uint8_t, 10>{0, 1, 2, 3, 4, 5, 6, 7, 8, 9});

SafeWrap<std::array<Sensor, 5>> sensorArraySafe(std::array<Sensor, 5>{Sensor(1, 25.5f, 60.0f, true),
  Sensor(2, 26.1f, 55.5f, true),
  Sensor(3, 24.8f, 62.3f, false),
  Sensor(4, 27.2f, 58.1f, true),
  Sensor(5, 25.9f, 61.7f, false)});

// Timeout for every operation in this example (SafeWrap always requires an explicit one).
static const TickType_t kTimeout = pdMS_TO_TICKS(10);

// A lone primitive value does not need SafeWrap: std::atomic is simpler and cheaper.
std::atomic<float> myFloat(25.0f);

void setup() {
  Serial.begin(115200);

  MyData snap{};
  if (myDataSafe.get(snap, kTimeout)) {
    Serial.printf("Initial struct data: %d, %d, %.2f\n", snap.data1, snap.data2, snap.data3);
  }

  bool updated = myDataSafe.withLock(
    [](MyData& data) {
      data.data1 = 42;
      data.data2 = 1234;
      data.data3 = 3.14f;
    },
    kTimeout);
  if (!updated) {
    Serial.println("Timeout while updating the struct");
  }

  if (myDataSafe.get(snap, kTimeout)) {
    Serial.printf("Updated struct data: %d, %d, %.2f\n", snap.data1, snap.data2, snap.data3);
  }

  // LockGuard: access through operator-> and early release with unlock().
  {
    auto lock = myDataSafe.getLock(kTimeout);
    if (lock) {
      lock->data1 += 1;
      lock.unlock(); // the mutex is free from here on
    }
  }

  Serial.printf("Initial float data: %.2f\n", myFloat.load());
  myFloat.store(100.5f);
  Serial.printf("Updated float data: %.2f\n", myFloat.load());

  Serial.printf("Initial array data: ");

  (void)myArraySafe.withLock(
    [](const std::array<uint8_t, 10>& arr) {
      for (size_t i = 0; i < arr.size(); ++i) {
        Serial.printf("%d ", arr[i]);
      }
      Serial.println();
    },
    kTimeout);

  (void)myArraySafe.withLock(
    [](std::array<uint8_t, 10>& arr) {
      for (size_t i = 0; i < arr.size(); ++i) {
        arr[i] += 10;
      }
    },
    kTimeout);

  Serial.printf("Updated array data: ");

  (void)myArraySafe.withLock(
    [](const std::array<uint8_t, 10>& arr) {
      for (size_t i = 0; i < arr.size(); ++i) {
        Serial.printf("%d ", arr[i]);
      }
      Serial.println();
    },
    kTimeout);

  Serial.println("\n=== Sensor Array ===");

  Serial.println("Initial sensors:");
  (void)sensorArraySafe.withLock(
    [](const std::array<Sensor, 5>& sensors) {
      for (size_t i = 0; i < sensors.size(); ++i) {
        Serial.printf("Sensor %d: ID=%d, Temp=%.1f°C, Hum=%.1f%%, Active=%s\n",
          i,
          sensors[i].id,
          sensors[i].temperature,
          sensors[i].humidity,
          sensors[i].isActive ? "Yes" : "No");
      }
    },
    kTimeout);

  (void)sensorArraySafe.withLock(
    [](std::array<Sensor, 5>& sensors) {
      sensors[2].isActive    = true;
      sensors[2].temperature = 26.5f;

      sensors[4].temperature += 1.5f;
      sensors[4].humidity -= 2.0f;

      for (auto& sensor : sensors) {
        if (sensor.isActive) {
          sensor.temperature += (random(-10, 11) / 10.0f);
        }
      }
    },
    kTimeout);

  Serial.println("\nSensors after update:");
  (void)sensorArraySafe.withLock(
    [](const std::array<Sensor, 5>& sensors) {
      for (size_t i = 0; i < sensors.size(); ++i) {
        Serial.printf("Sensor %d: ID=%d, Temp=%.1f°C, Hum=%.1f%%, Active=%s\n",
          i,
          sensors[i].id,
          sensors[i].temperature,
          sensors[i].humidity,
          sensors[i].isActive ? "Yes" : "No");
      }
    },
    kTimeout);
}

void performanceBenchmark() {
  const int iterations = 1000;

  unsigned long start = micros();
  for (int i = 0; i < iterations; i++) {
    (void)myArraySafe.withLock(
      [](std::array<uint8_t, 10>& arr) {
        for (size_t j = 0; j < arr.size(); ++j) {
          arr[j] = (arr[j] + 1) % 256;
        }
      },
      kTimeout);
  }
  unsigned long arrayTime = micros() - start;

  Serial.printf("std::array<uint8_t> performance: %lu microseconds for %d iterations\n",
    arrayTime,
    iterations);
  Serial.printf("Average per operation: %.2f microseconds\n", (float)arrayTime / iterations);

  start = micros();
  for (int i = 0; i < iterations; i++) {
    (void)sensorArraySafe.withLock(
      [](std::array<Sensor, 5>& sensors) {
        for (auto& sensor : sensors) {
          sensor.temperature += 0.1f;
          if (sensor.temperature > 30.0f) sensor.temperature = 20.0f;
        }
      },
      kTimeout);
  }
  unsigned long structArrayTime = micros() - start;

  Serial.printf("std::array<Sensor> performance: %lu microseconds for %d iterations\n",
    structArrayTime,
    iterations);
  Serial.printf("Average per operation: %.2f microseconds\n", (float)structArrayTime / iterations);
}

void sensorOperations() {
  Serial.println("\n=== Advanced Sensor Operations ===");

  (void)sensorArraySafe.withLock(
    [](const std::array<Sensor, 5>& sensors) {
      auto it =
        std::find_if(sensors.begin(), sensors.end(), [](const Sensor& s) { return s.id == 3; });

      if (it != sensors.end()) {
        Serial.printf("Found sensor ID 3: Temp=%.1f°C\n", it->temperature);
      }
    },
    kTimeout);

  int activeSensors = 0;
  (void)sensorArraySafe.withLock(
    [&activeSensors](const std::array<Sensor, 5>& sensors) {
      activeSensors =
        std::count_if(sensors.begin(), sensors.end(), [](const Sensor& s) { return s.isActive; });
    },
    kTimeout);
  Serial.printf("Active sensors: %d\n", activeSensors);

  float avgTemp = 0.0f;
  (void)sensorArraySafe.withLock(
    [&avgTemp, activeSensors](const std::array<Sensor, 5>& sensors) {
      float totalTemp = 0.0f;
      for (const auto& sensor : sensors) {
        if (sensor.isActive) {
          totalTemp += sensor.temperature;
        }
      }
      if (activeSensors > 0) {
        avgTemp = totalTemp / activeSensors;
      }
    },
    kTimeout);
  Serial.printf("Average temperature: %.1f°C\n", avgTemp);

  (void)sensorArraySafe.withLock(
    [](const std::array<Sensor, 5>& sensors) {
      auto maxTempSensor = std::max_element(sensors.begin(),
        sensors.end(),
        [](const Sensor& a, const Sensor& b) { return a.temperature < b.temperature; });

      Serial.printf("Hottest sensor: ID=%d, Temp=%.1f°C\n",
        maxTempSensor->id,
        maxTempSensor->temperature);
    },
    kTimeout);
}

void loop() {
  if (Serial.available()) {
    char c = Serial.read();

    switch (c) {
      case 'r':
      {
        ESP.restart();
      } break;
      case 'b':
      {
        performanceBenchmark();
      } break;
      case 's':
      {
        sensorOperations();
      } break;
      case 'h':
      {
        Serial.println("\n=== Available Commands ===");
        Serial.println("r - Restart ESP32");
        Serial.println("b - Performance benchmark");
        Serial.println("s - Sensor operations");
        Serial.println("h - Show help");
      } break;
    }
  }
}
