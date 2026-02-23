// sensors.h - Zmodernizowana wersja z NTC 100k
// [MOD] Oba DS18B20 = komora, NTC 100k na GPIO34 = mięso
#pragma once
#include <Arduino.h>

// Podstawowe funkcje
void requestTemperature();
void readTemperature();
void checkDoor();

// [NEW] Odczyt NTC 100k
double readNtcTemperature();

// Funkcje przypisywania czujników (teraz oba DS18B20 to komora)
void identifyAndAssignSensors();
bool autoDetectAndAssignSensors();

// Funkcje diagnostyczne
unsigned long getSensorCacheAge();
void forceSensorRead();
String getSensorDiagnostics();
String getSensorAssignmentInfo();

// Funkcje pomocnicze do web servera
int getChamberSensor1Index();
int getChamberSensor2Index();
int getTotalSensorCount();
bool areSensorsIdentified();

// Zmienne globalne
extern uint8_t sensorAddresses[2][8];
extern int chamberSensor1Index;
extern int chamberSensor2Index;
extern bool sensorsIdentified;
