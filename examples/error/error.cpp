#include "SafeWrap.h"
#include <atomic>
#include <vector>

#include <Arduino.h>

struct DatabaseRecord {
  int id;
  String name;
  std::vector<float> data;
  uint32_t lastAccess;
  bool processing;

  DatabaseRecord()
      : id(0)
      , name("")
      , lastAccess(0)
      , processing(false) {}

  // Deliberately slow (~100 ms) to cause contention.
  void heavyOperation() {
    processing = true;
    data.clear(); // bounded size: always 1000 samples
    for (int i = 0; i < 1000; i++) {
      data.push_back(random(0, 1000) / 10.0);
      delayMicroseconds(100);
    }
    lastAccess = millis();
    processing = false;
  }

  void quickRead() { lastAccess = millis(); }
};

SafeWrap<DatabaseRecord> sharedDatabase;

// Independent counters: std::atomic is enough, no need for SafeWrap.
std::atomic<int> successfulOperations(0);
std::atomic<int> timeoutErrors(0);
std::atomic<int> totalAttempts(0);

static const char* SEPARATOR = "==================================================";

void heavyProcessingTask(void* parameter) {
  Serial.println("Heavy Processing Task started");

  while (true) {
    ++totalAttempts;
    Serial.println("Heavy Task: trying to get the lock for heavy processing...");

    size_t elements = 0;
    bool success    = sharedDatabase.withLock(
      [&elements](DatabaseRecord& db) {
        db.heavyOperation();
        elements = db.data.size();
      },
      pdMS_TO_TICKS(500));

    if (success) {
      ++successfulOperations;
      Serial.printf("Heavy Task: OK - processing done. Data: %u elements\n", (unsigned)elements);
    } else {
      ++timeoutErrors;
      Serial.println("Heavy Task: TIMEOUT - could not get the lock");
    }

    vTaskDelay(pdMS_TO_TICKS(2000));
  }
}

void quickAccessTask(void* parameter) {
  Serial.println("Quick Access Task started");

  while (true) {
    ++totalAttempts;

    uint32_t lastAccess = 0;
    bool success        = sharedDatabase.withLock(
      [&lastAccess](DatabaseRecord& db) {
        lastAccess = db.lastAccess;
        db.quickRead();
      },
      pdMS_TO_TICKS(50));

    if (success) {
      ++successfulOperations;
      Serial.printf("Quick Task: OK - quick access. Last access: %lu\n", (unsigned long)lastAccess);
    } else {
      ++timeoutErrors;
      Serial.println("Quick Task: TIMEOUT - resource busy with heavy operation");
    }

    vTaskDelay(pdMS_TO_TICKS(300));
  }
}

void monitoringTask(void* parameter) {
  Serial.println("Monitoring Task started");

  while (true) {
    vTaskDelay(pdMS_TO_TICKS(5000));

    const int total    = totalAttempts.load();
    const int success  = successfulOperations.load();
    const int timeouts = timeoutErrors.load();

    Serial.printf("\n%s\n", SEPARATOR);
    Serial.println("CONTENTION STATISTICS");
    Serial.println(SEPARATOR);
    Serial.printf("Total attempts:       %d\n", total);
    Serial.printf("Successful:           %d\n", success);
    Serial.printf("Timeouts:             %d\n", timeouts);

    if (total > 0) {
      Serial.printf("Success rate:         %.1f%%\n", (float)success / total * 100);
      Serial.printf("Timeout rate:         %.1f%%\n", (float)timeouts / total * 100);
    }

    // Copy what is needed under the lock, print outside it.
    bool processing     = false;
    size_t elements     = 0;
    uint32_t lastAccess = 0;
    bool dbInfo         = sharedDatabase.withLock(
      [&](const DatabaseRecord& db) {
        processing = db.processing;
        elements   = db.data.size();
        lastAccess = db.lastAccess;
      },
      pdMS_TO_TICKS(10));

    if (dbInfo) {
      Serial.printf("DB state:             %s\n", processing ? "Processing" : "Available");
      Serial.printf("DB elements:          %u\n", (unsigned)elements);
      Serial.printf("Last access:          %lu ms\n", (unsigned long)lastAccess);
    } else {
      Serial.println("DB state:             not available (in use)");
    }

    Serial.printf("%s\n\n", SEPARATOR);
  }
}

void setup() {
  Serial.begin(115200);
  delay(1000);

  Serial.println("\nDEMO: Timeout Handling and Resource Contention");
  Serial.println("This example shows how to handle situations where");
  Serial.println("several tasks compete for the same resource.\n");

  xTaskCreatePinnedToCore(heavyProcessingTask, "HeavyTask", 4096, NULL, 2, NULL, 1);
  xTaskCreatePinnedToCore(quickAccessTask, "QuickTask", 3072, NULL, 1, NULL, 0);
  xTaskCreatePinnedToCore(monitoringTask, "MonitorTask", 3072, NULL, 1, NULL, 0);

  Serial.println("All tasks created. Watch them compete for the resource...\n");
}

void loop() { vTaskDelete(NULL); }
