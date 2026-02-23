// config.h - Zoptymalizowana wersja ze stabilnymi timeoutami
// [MOD] Zamiana SD na W25Q128 Flash (LittleFS)
// [MOD] Dodano PIN_NTC i stałe NTC 100k do pomiaru temperatury mięsa
#pragma once
#include <Arduino.h>
#include <WiFi.h>
#include <LittleFS.h>

// ======================================================
// 0. METADANE FIRMWARE
// ======================================================
#define FW_VERSION      "v3.6"
#define FW_AUTHOR       "Wojtek"
#define FW_NAME         "Wędzarnia IoT"
#define FW_FULL_NAME    FW_NAME " v" FW_VERSION

// ======================================================
// 1. DEFINICJE PINÓW
// ======================================================
#define PIN_ONEWIRE 4
#define PIN_NTC     34       // [NEW] Sonda NTC 100k - temperatura mięsa
#define PIN_SSR1 12
#define PIN_SSR2 13
#define PIN_SSR3 14
#define PIN_FAN 27
#define PIN_SMOKE_FAN 26
#define PIN_DOOR 25
#define PIN_BUZZER 33
#define PIN_BTN_UP      32
#define PIN_BTN_DOWN    17
#define PIN_BTN_ENTER   16
#define PIN_BTN_EXIT    21
#define PIN_FLASH_CS 5       // [MOD] CS dla zewnętrznego W25Q128 (16MB)
#define TFT_CS 15

// [NEW] Etykieta partycji dla zewnętrznego flash W25Q128
#define EXT_FLASH_PARTITION_LABEL "extfs"
#define TFT_DC 2
#define TFT_RST 22

// ======================================================
// [NEW] KONFIGURACJA NTC 100k
// ======================================================
constexpr double NTC_NOMINAL_R     = 100000.0;  // Rezystancja nominalna @ 25°C
constexpr double NTC_NOMINAL_T     = 25.0;       // Temperatura nominalna
constexpr double NTC_BETA          = 3950.0;     // Współczynnik Beta (typowy dla NTC 100k)
constexpr double NTC_SERIES_R      = 100000.0;   // Rezystor szeregowy w dzielniku napięcia (100k)
constexpr int    NTC_ADC_MAX       = 4095;        // Rozdzielczość ADC ESP32 (12-bit)
constexpr double NTC_V_REF         = 3.27;         // Napięcie referencyjne
constexpr int    NTC_SAMPLES       = 10;          // Liczba próbek do uśrednienia
constexpr double NTC_TEMP_MIN      = -10.0;       // Min akceptowalna temperatura
constexpr double NTC_TEMP_MAX      = 200.0;       // Max akceptowalna temperatura

// ======================================================
// 2. KONFIGURACJA GLOBALNA
// ======================================================

// --- Wyświetlacz ---
constexpr int SCREEN_WIDTH = 128;
constexpr int SCREEN_HEIGHT = 160;
#define ST77XX_DARKGREY 0x7BEF

// --- LEDC (PWM) ---
constexpr int LEDC_FREQ = 5000;
constexpr int LEDC_RESOLUTION = 8;

// --- PID ---
constexpr double CFG_Kp = 5.0;
constexpr double CFG_Ki = 0.3;
constexpr double CFG_Kd = 20.0;

// --- Limity ---
constexpr double CFG_T_MAX_SOFT = 130.0;
constexpr double CFG_T_MIN_SET = 20.0;
constexpr double CFG_T_MAX_SET = 120.0;
constexpr unsigned long CFG_MAX_PROCESS_TIME_MS = 24UL * 60UL * 60UL * 1000UL;
constexpr int CFG_SMOKE_PWM_MIN = 0;
constexpr int CFG_SMOKE_PWM_MAX = 255;
constexpr int CFG_POWERMODE_MIN = 1;
constexpr int CFG_POWERMODE_MAX = 3;

// --- Domyślne wartości ---
constexpr unsigned long CFG_FAN_ON_DEFAULT_MS = 10000;
constexpr unsigned long CFG_FAN_OFF_DEFAULT_MS = 60000;

// --- WiFi / Web ---
constexpr const char* CFG_AP_SSID = "Wedzarnia";
constexpr const char* CFG_AP_PASS = "12345678";

// --- WiFi Auto-Reconnect ---
constexpr unsigned long CFG_WIFI_CHECK_INTERVAL = 5000;
constexpr unsigned long CFG_WIFI_RETRY_MIN_DELAY = 5000;
constexpr unsigned long CFG_WIFI_RETRY_MAX_DELAY = 60000;

// --- GitHub Repo for Profiles ---
constexpr const char* CFG_GITHUB_API_URL = "https://api.github.com/repos/kudys11/wedzarnia-przepisy/contents/profiles";
constexpr const char* CFG_GITHUB_PROFILES_BASE_URL = "https://raw.githubusercontent.com/kudys11/wedzarnia-przepisy/main/profiles/";

// --- Watchdog ---
constexpr int WDT_TIMEOUT = 10;
constexpr unsigned long SOFT_WDT_TIMEOUT = 30000;
constexpr unsigned long TASK_WATCHDOG_TIMEOUT = 10000;

// --- Czujniki ---
constexpr unsigned long TEMP_REQUEST_INTERVAL = 1200;
constexpr unsigned long TEMP_CONVERSION_TIME = 850;
constexpr int SENSOR_ERROR_THRESHOLD = 3;
constexpr unsigned long SENSOR_READ_TIMEOUT = 100;

// --- Stałe przypisania czujników ---
// [MOD] Oba DS18B20 to czujniki komory, NTC to mięso
constexpr int DEFAULT_CHAMBER_SENSOR_1 = 0;
constexpr int DEFAULT_CHAMBER_SENSOR_2 = 1;
constexpr unsigned long SENSOR_ASSIGNMENT_CHECK = 10000;

// --- Profil ---
constexpr int MAX_STEPS = 10;

// --- Timeouty dla mutexów ---
constexpr TickType_t CFG_MUTEX_TIMEOUT_MS = 1000;

// --- Logging ---
constexpr int LOG_LEVEL_DEBUG = 0;
constexpr int LOG_LEVEL_INFO = 1;
constexpr int LOG_LEVEL_WARN = 2;
constexpr int LOG_LEVEL_ERROR = 3;
constexpr int CURRENT_LOG_LEVEL = LOG_LEVEL_INFO;

// --- Adaptive PID ---
constexpr unsigned long PID_ADAPTATION_INTERVAL = 60000;

// --- Progi pamięci ---
constexpr uint32_t HEAP_WARNING_THRESHOLD = 20000;
constexpr uint32_t HEAP_CRITICAL_THRESHOLD = 10000;

// --- Debouncing ---
constexpr unsigned long DEBOUNCE_DELAY = 100;
constexpr unsigned long LONG_PRESS_DURATION = 1000;

// ======================================================
// [NEW] AUTORYZACJA HTTP Basic Auth
// ======================================================
constexpr const char* CFG_AUTH_DEFAULT_USER = "admin";
constexpr const char* CFG_AUTH_DEFAULT_PASS = "wedzarnia";
constexpr unsigned long CFG_AUTH_RESET_HOLD_MS = 5000;

// ======================================================
// [NEW] ZABEZPIECZENIE: GRZAŁKA BEZ WZROSTU TEMPERATURY
// ======================================================
constexpr unsigned long HEATER_NO_RISE_TIMEOUT_MS = 20UL * 60UL * 1000UL;
constexpr double HEATER_MIN_TEMP_RISE     = 2.0;
constexpr double HEATER_FAULT_MIN_PID     = 50.0;
constexpr double HEATER_FAULT_MIN_ERROR   = 10.0;

// ======================================================
// 3. DEFINICJE TYPÓW I STRUKTUR
// ======================================================

enum class ProcessState {
    IDLE,
    RUNNING_AUTO,
    RUNNING_MANUAL,
    PAUSE_DOOR,
    PAUSE_SENSOR,
    PAUSE_OVERHEAT,
    PAUSE_USER,
    ERROR_PROFILE,
    SOFT_RESUME,
    PAUSE_HEATER_FAULT
};

enum class RunMode {
    MODE_AUTO,
    MODE_MANUAL
};

enum class ErrorCode {
    NONE = 0,
    FLASH_INIT_FAILED,    // [MOD] Zmieniono z SD_INIT_FAILED
    SENSOR_DISCONNECTED,
    OVERHEAT,
    PROFILE_INVALID,
    WIFI_FAILED,
    MUTEX_TIMEOUT,
    TASK_TIMEOUT
};

struct HeaterEnable {
    bool h1, h2, h3;
    unsigned long t1, t2, t3;
};

struct Step {
    char name[32];
    double tSet;
    double tMeatTarget;
    unsigned long minTimeMs;
    int powerMode;
    int smokePwm;
    int fanMode;
    unsigned long fanOnTime;
    unsigned long fanOffTime;
    bool useMeatTemp;
};

struct ProcessStats {
    unsigned long totalRunTime;
    unsigned long activeHeatingTime;
    int stepChanges;
    int pauseCount;
    double avgTemp;
    unsigned long lastUpdate;
    unsigned long totalProcessTimeSec;
    unsigned long remainingProcessTimeSec;
};

// ======================================================
// 4. FUNKCJE POMOCNICZE
// ======================================================

inline void log_msg(int level, const char* msg) {
    if (level >= CURRENT_LOG_LEVEL) {
        static const char* const prefix[] = {"[DBG]", "[INF]", "[WRN]", "[ERR]"};
        Serial.printf("%s %s\n", prefix[level], msg);
    }
}

inline void log_msg(int level, const String& msg) {
    log_msg(level, msg.c_str());
}

#define LOG_BUF_SIZE 128
#define LOG_FMT(level, fmt, ...) do { \
    if ((level) >= CURRENT_LOG_LEVEL) { \
        char _log_buf[LOG_BUF_SIZE]; \
        snprintf(_log_buf, sizeof(_log_buf), fmt, ##__VA_ARGS__); \
        log_msg(level, _log_buf); \
    } \
} while(0)
