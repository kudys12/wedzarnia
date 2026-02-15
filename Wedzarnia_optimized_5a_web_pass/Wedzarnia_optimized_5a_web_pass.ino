// Wedzarnia_optimized_v3.3.ino - Uproszczona wersja testów
#include "config.h"
#include "state.h"
#include "hardware.h"
#include "storage.h"
#include "web_server.h"
#include "tasks.h"
#include "outputs.h"
#include "ui.h"
#include <esp_task_wdt.h>

void setup() {
    Serial.begin(115200);
    delay(100);
    
    Serial.println("\n==========================================================");
    Serial.println("     WEDZARNIA ESP32 v3.4 (Simplified Tests)   ");
    Serial.println("     by Wojtek - Optimized Startup              ");
    Serial.println("==========================================================\n");
    
    log_msg(LOG_LEVEL_INFO, "Starting initialization sequence...");
    
    // 1. Inicjalizacja NVS
    nvs_init();
    esp_task_wdt_reset();
    
    // 2. Inicjalizacja mutexow stanu
    init_state();
    esp_task_wdt_reset();
    
    // 3. Inicjalizacja pinow GPIO
    hardware_init_pins();
    esp_task_wdt_reset();

    // 4. Inicjalizacja UI
    ui_init();
    esp_task_wdt_reset();
   
    // 5. Inicjalizacja PWM/LEDC
    hardware_init_ledc();
    esp_task_wdt_reset();
    
    // 6. Inicjalizacja wyswietlacza
    hardware_init_display();
    esp_task_wdt_reset();
    
    // 7. Inicjalizacja czujnikow temperatury
    hardware_init_sensors();
    esp_task_wdt_reset();
    
    // 8. Inicjalizacja karty SD (z retry)
    hardware_init_sd();
    esp_task_wdt_reset();

    // 9. Wczytaj konfiguracje z NVS
    storage_load_config_nvs();
    esp_task_wdt_reset();
    
    // 10. Identyfikacja i przypisanie czujnikow
    log_msg(LOG_LEVEL_INFO, "Starting sensor identification...");
    
    // 11. Uruchom uproszczona diagnostyke startowa
    runStartupSelfTest();
    esp_task_wdt_reset();
    
    // 12. Inicjalizacja WiFi
    hardware_init_wifi();
    esp_task_wdt_reset();
    
    // Debug: Sprawdz czy WiFi dziala
    if (WiFi.status() == WL_CONNECTED) {
        Serial.printf("\nWiFi Connected! IP: %s\n", WiFi.localIP().toString().c_str());
    } else {
        Serial.println("\nWiFi in AP mode only");
        Serial.printf("AP IP: %s\n", WiFi.softAPIP().toString().c_str());
    }
    
    esp_task_wdt_reset();
    
    // 13. Inicjalizacja serwera WWW
    web_server_init();
    esp_task_wdt_reset();
    
    // Sygnal dzwiekowy - gotowe
    buzzerBeep(2, 100, 100);
    
    // Wyswietl informacje o systemie na Serial
    Serial.println("\n==========================================================");
    Serial.println("     SYSTEM READY - DETAILED INFO              ");
    Serial.println("==========================================================");
    Serial.printf("Free heap: %d bytes\n", ESP.getFreeHeap());
    Serial.printf("Min free heap: %d bytes\n", ESP.getMinFreeHeap());
    Serial.printf("CPU freq: %d MHz\n", ESP.getCpuFreqMHz());
    Serial.printf("SD Card: %s\n", SD.cardType() != CARD_NONE ? "OK" : "ERROR");
    Serial.printf("Sensors found: %d\n", sensors.getDeviceCount());
    Serial.printf("WiFi: %s\n", WiFi.status() == WL_CONNECTED ? "CONNECTED" : "AP MODE");
    Serial.println("==========================================================\n");
    
    log_msg(LOG_LEVEL_INFO, "==========================================================");
    log_msg(LOG_LEVEL_INFO, "     SETUP COMPLETE - STARTING TASKS           ");
    log_msg(LOG_LEVEL_INFO, "==========================================================");
    
    // 14. Uruchom zadania FreeRTOS
    tasks_create_all();
    
    // Wyswietl informacje o systemie w logach
    log_msg(LOG_LEVEL_INFO, "[SYS] Free heap: " + String(ESP.getFreeHeap()) + " bytes");
    log_msg(LOG_LEVEL_INFO, "[SYS] Min free heap: " + String(ESP.getMinFreeHeap()) + " bytes");
    log_msg(LOG_LEVEL_INFO, "[SYS] CPU freq: " + String(ESP.getCpuFreqMHz()) + " MHz");
    log_msg(LOG_LEVEL_INFO, "[SYS] SD Card: " + String(SD.cardType() != CARD_NONE ? "OK" : "ERROR"));
    log_msg(LOG_LEVEL_INFO, "[SYS] Sensors: " + String(sensors.getDeviceCount()));
    log_msg(LOG_LEVEL_INFO, "[SYS] WiFi: " + String(WiFi.status() == WL_CONNECTED ? "CONNECTED" : "AP MODE"));
    log_msg(LOG_LEVEL_INFO, "\nSystem ready!\n");
    
    Serial.println("\nV System initialization complete!");
    Serial.println("V ESP32 Wedzarnia Ready!");
    Serial.println("\nGo to http://" + WiFi.softAPIP().toString() + " for web interface");
    
    // Finalny sygnal
    buzzerBeep(3, 150, 100);
}

void loop() {
    // Regularne sprawdzanie czujnikow (jesli nie sa zidentyfikowane)
    static unsigned long lastSensorCheck = 0;
    if (millis() - lastSensorCheck > SENSOR_ASSIGNMENT_CHECK) {
        lastSensorCheck = millis();
        // Czujniki sa teraz automatycznie identyfikowane w sensors.cpp
    }
    
    // Debug: Okazjonalne logowanie stanu przyciskow (co 60 sekund)
    static unsigned long lastButtonDebug = 0;
    if (millis() - lastButtonDebug > 60000) {
        lastButtonDebug = millis();
        log_msg(LOG_LEVEL_DEBUG, 
            "Button States: UP=" + String(digitalRead(PIN_BTN_UP)) +
            " DOWN=" + String(digitalRead(PIN_BTN_DOWN)) +
            " ENTER=" + String(digitalRead(PIN_BTN_ENTER)) +
            " EXIT=" + String(digitalRead(PIN_BTN_EXIT)) +
            " DOOR=" + String(digitalRead(PIN_DOOR)));
    }
    
    // Pusta petla - wszystko dzieje sie w zadaniach RTOS
    vTaskDelay(pdMS_TO_TICKS(1000));
    
    // Monitoring pamieci co 10 sekund
    static unsigned long loopCounter = 0;
    loopCounter++;
    if (loopCounter % 10 == 0) {
        uint32_t freeHeap = ESP.getFreeHeap();
        if (freeHeap < 20000) { // Zwiększony próg z 15KB do 20KB
            log_msg(LOG_LEVEL_WARN, "Low heap memory: " + String(freeHeap) + " bytes");
        }
    }
}
