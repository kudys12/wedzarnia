// sensors.h - Zmodernizowana wersja z funkcjami przypisywania
#pragma once
#include <Arduino.h>

// Podstawowe funkcje
void requestTemperature();
void readTemperature();
void checkDoor();

// Funkcje przypisywania czujników
void identifyAndAssignSensors();
void reassignSensors(int newChamberIndex, int newMeatIndex);
bool autoDetectAndAssignSensors();

// Funkcje diagnostyczne
unsigned long getSensorCacheAge();
void forceSensorRead();
String getSensorDiagnostics();
String getSensorAssignmentInfo();

// Funkcje pomocnicze do web servera - DODAJEMY TE DEKLARACJE
int getChamberSensorIndex();
int getMeatSensorIndex();
int getTotalSensorCount();
bool areSensorsIdentified();

// Funkcje do zmiennych globalnych (jeśli potrzebne bezpośrednio)
extern uint8_t sensorAddresses[2][8];
extern int chamberSensorIndex;     // Dodajemy extern
extern int meatSensorIndex;        // Dodajemy extern
extern bool sensorsIdentified;     // Dodajemy extern