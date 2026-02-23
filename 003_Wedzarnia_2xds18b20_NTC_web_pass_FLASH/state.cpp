// state.cpp - Zoptymalizowana wersja z timeoutami i statystykami
// [MOD] Dodano g_tChamber1, g_tChamber2
#include "state.h"

// Definicje obiektów globalnych
Adafruit_ST7735 display(TFT_CS, TFT_DC, TFT_RST);
WebServer server(80);
OneWire oneWire(PIN_ONEWIRE);
DallasTemperature sensors(&oneWire);

double pidInput, pidSetpoint;
double pidOutput = 0;
PID pid(&pidInput, &pidOutput, &pidSetpoint, CFG_Kp, CFG_Ki, CFG_Kd, DIRECT);

SemaphoreHandle_t stateMutex = NULL;
SemaphoreHandle_t outputMutex = NULL;
SemaphoreHandle_t heaterMutex = NULL;

// Definicje zmiennych stanu
volatile ProcessState g_currentState = ProcessState::IDLE;
RunMode g_lastRunMode = RunMode::MODE_AUTO;
volatile double g_tSet = 70.0;
volatile double g_tChamber = 25.0;       // średnia z dwóch DS18B20
volatile double g_tChamber1 = 25.0;      // [NEW] DS18B20 #1
volatile double g_tChamber2 = 25.0;      // [NEW] DS18B20 #2
volatile double g_tMeat = 25.0;          // [MOD] teraz z NTC 100k
volatile int g_powerMode = 1;
volatile int g_manualSmokePwm = 0;
volatile int g_fanMode = 1;
volatile unsigned long g_fanOnTime = CFG_FAN_ON_DEFAULT_MS;
volatile unsigned long g_fanOffTime = CFG_FAN_OFF_DEFAULT_MS;
volatile bool g_doorOpen = false;
volatile bool g_errorSensor = false;
volatile bool g_errorOverheat = false;
volatile bool g_errorProfile = false;

Step g_profile[MAX_STEPS];
int g_stepCount = 0;
int g_currentStep = 0;
unsigned long g_processStartTime = 0;
unsigned long g_stepStartTime = 0;

// Statystyki procesu
ProcessStats g_processStats = {0, 0, 0, 0, 0.0, 0, 0, 0};

// Funkcje blokowania z timeoutami
bool state_lock(TickType_t timeout_ms) {
    if (!stateMutex) return false;
    if (xSemaphoreTake(stateMutex, pdMS_TO_TICKS(timeout_ms)) != pdTRUE) {
        log_msg(LOG_LEVEL_WARN, "state_lock timeout!");
        return false;
    }
    return true;
}

void state_unlock() {
    if (stateMutex) xSemaphoreGive(stateMutex);
}

bool output_lock(TickType_t timeout_ms) {
    if (!outputMutex) return false;
    if (xSemaphoreTake(outputMutex, pdMS_TO_TICKS(timeout_ms)) != pdTRUE) {
        log_msg(LOG_LEVEL_WARN, "output_lock timeout!");
        return false;
    }
    return true;
}

void output_unlock() {
    if (outputMutex) xSemaphoreGive(outputMutex);
}

bool heater_lock(TickType_t timeout_ms) {
    if (!heaterMutex) return false;
    if (xSemaphoreTake(heaterMutex, pdMS_TO_TICKS(timeout_ms)) != pdTRUE) {
        log_msg(LOG_LEVEL_WARN, "heater_lock timeout!");
        return false;
    }
    return true;
}

void heater_unlock() {
    if (heaterMutex) xSemaphoreGive(heaterMutex);
}

void init_state() {
    stateMutex = xSemaphoreCreateMutex();
    outputMutex = xSemaphoreCreateMutex();
    heaterMutex = xSemaphoreCreateMutex();
    log_msg(LOG_LEVEL_INFO, "State mutexes initialized");
}
