// hardware.h - Zmodernizowana wersja
#pragma once
#include <Arduino.h>

// Podstawowe funkcje inicjalizacji
void hardware_init_pins();
void hardware_init_ledc();
void hardware_init_sensors();
void hardware_init_display();
void hardware_init_sd();
void nvs_init();
void hardware_init_wifi();

// Nowe funkcje diagnostyczne
void initLoggingSystem();
void logToFile(const String& message);
void runStartupSelfTest();
void testOutput(int pin, const char* name);
void testButton(int pin, const char* name);
void deleteOldestLog(const char* dirPath);