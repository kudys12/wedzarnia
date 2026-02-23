// hardware.h
#pragma once
#include <Arduino.h>
#include <esp_flash.h>

#ifndef EXT_FLASH_PARTITION_LABEL
#define EXT_FLASH_PARTITION_LABEL "extfs"
#endif

// Podstawowe funkcje inicjalizacji
void hardware_init_pins();
void hardware_init_ledc();
void hardware_init_sensors();
void hardware_init_display();
void hardware_init_flash();
void nvs_init();
void hardware_init_wifi();

// Diagnostyka i logging
void initLoggingSystem();
void logToFile(const String& message);
void runStartupSelfTest();
void testOutput(int pin, const char* name);
void testButton(int pin, const char* name);
void deleteOldestLog(const char* dirPath);

// Zewnętrzny flash - dostęp dla storage.cpp i web_server.cpp
uint32_t     hardware_get_ext_flash_size();
esp_flash_t* hardware_get_ext_flash_handle();

// Remount po LittleFS.end() - ZAWSZE używaj zamiast LittleFS.begin(true/false)!
// format=true  -> wyczyść i zamontuj od nowa
// format=false -> tylko zamontuj (spróbuje format jeśli mount się nie uda)
bool hardware_remount_flash(bool format = false);
