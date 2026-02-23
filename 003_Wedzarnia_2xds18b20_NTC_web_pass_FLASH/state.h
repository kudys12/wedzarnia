// state.h - Zoptymalizowana wersja
// [MOD] Dodano g_tChamber1, g_tChamber2 dla dwóch czujników DS18B20 komory
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
extern volatile double g_tChamber;      // średnia temperatura komory
extern volatile double g_tChamber1;     // [NEW] DS18B20 #1 - komora
extern volatile double g_tChamber2;     // [NEW] DS18B20 #2 - komora
extern volatile double g_tMeat;         // [MOD] teraz z NTC 100k
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
