#include <Arduino.h>
#include "app_manager.h"

void setup() {
    Serial.begin(115200);
    
    // Give USB CDC host time to attach
    uint32_t start = millis();
    while (!Serial && (millis() - start < 3000)) {
        delay(10);
    }
    
    Serial.println("\n\n========================================");
    Serial.println("   LilyGO T-Display-S3 OS Booting...   ");
    Serial.println("========================================");
    Serial.printf("Chip Revision: %d\n", ESP.getChipRevision());
    Serial.printf("Free Heap: %u bytes\n", ESP.getFreeHeap());
    Serial.printf("Free PSRAM: %u bytes\n", ESP.getFreePsram());
    Serial.println("========================================\n");

    AppManager::getInstance().begin();
}

void loop() {
    AppManager::getInstance().update();
    delay(2); // minimal yield for FreeRTOS scheduler
}

