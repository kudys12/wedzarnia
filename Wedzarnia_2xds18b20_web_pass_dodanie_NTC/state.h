// state.h - Zoptymalizowana wersja
#pragma once
#include <Adafruit_ST7735.h>
#include <DallasTemperature.h>
#include <PID_v1.h>
#include <WebServer.h>
#include "config.h"

// Deklaracje extern dla obiektów globalnych
extern Adafruit_ST7735 display;
extern WebServer server;
extern OneWire oneWire;
extern DallasTemperature sensors;
extern PID pid;
extern SemaphoreHandle_t stateMutex;
extern SemaphoreHandle_t outputMutex;
extern SemaphoreHandle_t heaterMutex;

// PID output
extern double pidOutput;
extern double pidInput;
extern double pidSetpoint;

// Deklaracje extern dla zmiennych stanu
extern volatile ProcessState g_currentState;
extern RunMode g_lastRunMode;
extern volatile double g_tSet;
extern volatile double g_tChamber;
extern volatile double g_tMeat;
extern volatile int g_powerMode;
extern volatile int g_manualSmokePwm;
extern volatile int g_fanMode;
extern volatile unsigned long g_fanOnTime;
extern volatile unsigned long g_fanOffTime;
extern volatile bool g_doorOpen;
extern volatile bool g_errorSensor;
extern volatile bool g_errorOverheat;
extern volatile bool g_errorProfile;

extern Step g_profile[MAX_STEPS];
extern int g_stepCount;
extern int g_currentStep;
extern unsigned long g_processStartTime;
extern unsigned long g_stepStartTime;

// Statystyki procesu
extern ProcessStats g_processStats;

// Funkcje pomocnicze do blokowania z timeoutami
bool state_lock(TickType_t timeout_ms = CFG_MUTEX_TIMEOUT_MS);
void state_unlock();
bool output_lock(TickType_t timeout_ms = CFG_MUTEX_TIMEOUT_MS);
void output_unlock();
bool heater_lock(TickType_t timeout_ms = CFG_MUTEX_TIMEOUT_MS);
void heater_unlock();

void init_state();

// Deklaracje z storage.h (żeby uniknąć cyklicznych zależności)
bool storage_reinit_sd();
String storage_get_profile_as_json(const char* profileName);

// Dodane deklaracje z sensors.h dla przypisań czujników
//extern int chamberSensorIndex;
//extern int meatSensorIndex;
//extern bool sensorsIdentified;
void identifyAndAssignSensors();
void reassignSensors(int newChamberIndex, int newMeatIndex);