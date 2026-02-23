// hardware.cpp - FINAL FIX
// Kluczowe zmiany:
// 1. ext_flash i ext_flash_size_bytes są eksportowane przez hardware_get_ext_flash_handle()
//    żeby storage_reinit_flash() i handleFlashFormat() mogły ponownie zamontować
//    zewnętrzny chip po LittleFS.end()
// 2. Dodana funkcja hardware_remount_flash() - wielokrotny remount bez reinicjalizacji SPI

#include "hardware.h"
#include "config.h"

#ifndef EXT_FLASH_PARTITION_LABEL
#define EXT_FLASH_PARTITION_LABEL "extfs"
#endif

#include "state.h"
#include "outputs.h"
#include "wifimanager.h"
#include <LittleFS.h>
#include <nvs_flash.h>
#include <WiFi.h>
#include <esp_task_wdt.h>
#include <SPI.h>
#include <esp_flash.h>
#include <esp_flash_spi_init.h>
#include <esp_partition.h>
#include <esp_littlefs.h>

// Globalne - dostępne przez hardware.h dla storage.cpp i web_server.cpp
static esp_flash_t* ext_flash            = nullptr;
static uint32_t     ext_flash_size_bytes = 0;
static bool         ext_flash_ready      = false;

uint32_t hardware_get_ext_flash_size()   { return ext_flash_size_bytes; }
esp_flash_t* hardware_get_ext_flash_handle() { return ext_flash; }

// ======================================================
// hardware_remount_flash()
// Wywołaj po każdym LittleFS.end() zamiast LittleFS.begin(true)
// Montuje zewnętrzny chip z właściwą etykietą + formatuje jeśli trzeba
// ======================================================
bool hardware_remount_flash(bool format) {
    if (!ext_flash || ext_flash_size_bytes == 0) {
        Serial.println("[FLASH] remount: ext_flash not initialized!");
        return false;
    }

    // Partycja może już być zarejestrowana - znajdź ją
    const esp_partition_t* part = esp_partition_find_first(
        ESP_PARTITION_TYPE_DATA,
        ESP_PARTITION_SUBTYPE_DATA_SPIFFS,
        EXT_FLASH_PARTITION_LABEL
    );

    // Jeśli nie znaleziono - zarejestruj ponownie
    if (!part) {
        Serial.println("[FLASH] remount: re-registering partition...");
        esp_err_t err = esp_partition_register_external(
            ext_flash, 0, ext_flash_size_bytes,
            EXT_FLASH_PARTITION_LABEL,
            ESP_PARTITION_TYPE_DATA,
            ESP_PARTITION_SUBTYPE_DATA_SPIFFS,
            &part
        );
        if (err != ESP_OK || !part) {
            Serial.printf("[FLASH] remount: register failed: %s\n", esp_err_to_name(err));
            return false;
        }
    }

    if (format) {
        Serial.println("[FLASH] remount: formatting...");
        esp_err_t fmt = esp_littlefs_format(EXT_FLASH_PARTITION_LABEL);
        Serial.printf("[FLASH] remount: format=%s\n", esp_err_to_name(fmt));
        if (fmt != ESP_OK) return false;
        delay(200);
    }

    // Montuj z etykietą - nigdy bez!
    bool ok = LittleFS.begin(false, "/littlefs", 10, EXT_FLASH_PARTITION_LABEL);
    if (!ok && !format) {
        // Spróbuj z formatowaniem
        Serial.println("[FLASH] remount: mount failed, trying with format...");
        esp_littlefs_format(EXT_FLASH_PARTITION_LABEL);
        delay(200);
        ok = LittleFS.begin(false, "/littlefs", 10, EXT_FLASH_PARTITION_LABEL);
    }

    if (ok) {
        // Upewnij się że katalogi istnieją
        if (!LittleFS.exists("/profiles")) {
            File f = LittleFS.open("/profiles/.keep", "w");
            if (f) f.close();
        }
        if (!LittleFS.exists("/logs")) {
            File f = LittleFS.open("/logs/.keep", "w");
            if (f) f.close();
        }
        if (!LittleFS.exists("/backup")) {
            File f = LittleFS.open("/backup/.keep", "w");
            if (f) f.close();
        }
        Serial.printf("[FLASH] remount OK: %uKB total, %uKB free\n",
            LittleFS.totalBytes()/1024, (LittleFS.totalBytes()-LittleFS.usedBytes())/1024);
    } else {
        Serial.println("[FLASH] remount: FAILED!");
    }

    return ok;
}

// ======================================================
void hardware_init_pins() {
    pinMode(PIN_SSR1,      OUTPUT);
    pinMode(PIN_SSR2,      OUTPUT);
    pinMode(PIN_SSR3,      OUTPUT);
    pinMode(PIN_FAN,       OUTPUT);
    pinMode(PIN_SMOKE_FAN, OUTPUT);
    pinMode(PIN_BUZZER,    OUTPUT);

    pinMode(PIN_DOOR,      INPUT_PULLUP);
    pinMode(PIN_BTN_UP,    INPUT_PULLUP);
    pinMode(PIN_BTN_DOWN,  INPUT_PULLUP);
    pinMode(PIN_BTN_ENTER, INPUT_PULLUP);
    pinMode(PIN_BTN_EXIT,  INPUT_PULLUP);

    analogReadResolution(12);
    LOG_FMT(LOG_LEVEL_INFO, "NTC pin GPIO%d configured", PIN_NTC);
    log_msg(LOG_LEVEL_INFO, "GPIO pins initialized");
}

void hardware_init_ledc() {
    bool ok = true;
    if (!ledcAttach(PIN_SSR1,      LEDC_FREQ, LEDC_RESOLUTION)) { log_msg(LOG_LEVEL_ERROR, "LEDC SSR1 fail!");  ok = false; }
    if (!ledcAttach(PIN_SSR2,      LEDC_FREQ, LEDC_RESOLUTION)) { log_msg(LOG_LEVEL_ERROR, "LEDC SSR2 fail!");  ok = false; }
    if (!ledcAttach(PIN_SSR3,      LEDC_FREQ, LEDC_RESOLUTION)) { log_msg(LOG_LEVEL_ERROR, "LEDC SSR3 fail!");  ok = false; }
    if (!ledcAttach(PIN_SMOKE_FAN, LEDC_FREQ, LEDC_RESOLUTION)) { log_msg(LOG_LEVEL_ERROR, "LEDC SMOKE fail!"); ok = false; }
    allOutputsOff();
    if (ok) log_msg(LOG_LEVEL_INFO, "LEDC/PWM initialized");
}

void hardware_init_sensors() {
    sensors.begin();
    sensors.setWaitForConversion(false);
    sensors.setResolution(12);
    int n = sensors.getDeviceCount();
    LOG_FMT(LOG_LEVEL_INFO, "Found %d DS18B20 sensor(s)", n);
    if (n == 0) log_msg(LOG_LEVEL_WARN, "No temperature sensors found!");
}

void hardware_init_display() {
    display.initR(INITR_BLACKTAB);
    display.setRotation(0);
    display.fillScreen(ST77XX_BLACK);
    display.setCursor(10, 20);
    display.setTextColor(ST77XX_WHITE);
    display.setTextSize(2);
    display.println("WEDZARNIA");
    display.setTextSize(1);
    display.println("\n   " FW_VERSION);
    display.println("   by " FW_AUTHOR);
    display.println("\n   Inicjalizacja...");
    delay(1500);
    log_msg(LOG_LEVEL_INFO, "Display initialized");
}

// ======================================================
// INICJALIZACJA ZEWNĘTRZNEGO FLASH
// ======================================================
void hardware_init_flash() {
    esp_task_wdt_reset();
    Serial.println("[FLASH] === INIT START ===");

    gpio_set_direction((gpio_num_t)TFT_CS,       GPIO_MODE_OUTPUT);
    gpio_set_level    ((gpio_num_t)TFT_CS,       1);
    gpio_set_direction((gpio_num_t)PIN_FLASH_CS,  GPIO_MODE_OUTPUT);
    gpio_set_level    ((gpio_num_t)PIN_FLASH_CS,  1);
    delay(100);

    SPI.end();
    delay(100);

    spi_bus_config_t bus_cfg = {};
    bus_cfg.mosi_io_num     = 23;
    bus_cfg.miso_io_num     = 19;
    bus_cfg.sclk_io_num     = 18;
    bus_cfg.quadwp_io_num   = -1;
    bus_cfg.quadhd_io_num   = -1;
    bus_cfg.max_transfer_sz = 4096;

    esp_err_t err = spi_bus_initialize(SPI2_HOST, &bus_cfg, SPI_DMA_CH_AUTO);
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
        Serial.printf("[FLASH] SPI2 FAILED: %s\n", esp_err_to_name(err));
        SPI.begin(18, 19, 23, -1);
        if (state_lock()) { g_errorProfile = true; state_unlock(); }
        buzzerBeep(3, 200, 200);
        return;
    }
    Serial.printf("[FLASH] SPI2: %s\n", esp_err_to_name(err));

    esp_flash_spi_device_config_t dev_cfg = {};
    dev_cfg.host_id        = SPI2_HOST;
    dev_cfg.cs_id          = 0;
    dev_cfg.cs_io_num      = PIN_FLASH_CS;
    dev_cfg.io_mode        = SPI_FLASH_SLOWRD;
    dev_cfg.freq_mhz       = 20;
    dev_cfg.input_delay_ns = 0;

    for (int attempt = 1; attempt <= 3; attempt++) {
        esp_task_wdt_reset();
        Serial.printf("[FLASH] Attempt %d/3\n", attempt);

        ext_flash = nullptr;
        err = spi_bus_add_flash_device(&ext_flash, &dev_cfg);
        if (err != ESP_OK || !ext_flash) {
            Serial.printf("[FLASH] add_flash_device: %s\n", esp_err_to_name(err));
            if (attempt < 3) { delay(1000); continue; } break;
        }

        err = esp_flash_init(ext_flash);
        if (err != ESP_OK) {
            Serial.printf("[FLASH] flash_init: %s\n", esp_err_to_name(err));
            spi_bus_remove_flash_device(ext_flash); ext_flash = nullptr;
            if (attempt < 3) { delay(1000); continue; } break;
        }

        err = esp_flash_get_size(ext_flash, &ext_flash_size_bytes);
        if (err != ESP_OK || ext_flash_size_bytes == 0) {
            Serial.printf("[FLASH] get_size: %s\n", esp_err_to_name(err));
            spi_bus_remove_flash_device(ext_flash); ext_flash = nullptr;
            if (attempt < 3) { delay(1000); continue; } break;
        }

        uint32_t jedec = 0;
        esp_flash_read_id(ext_flash, &jedec);
        Serial.printf("[FLASH] JEDEC=0x%06X  Size=%uMB\n",
            jedec, ext_flash_size_bytes / (1024*1024));

        const esp_partition_t* ext_part = nullptr;
        err = esp_partition_register_external(
            ext_flash, 0, ext_flash_size_bytes,
            EXT_FLASH_PARTITION_LABEL,
            ESP_PARTITION_TYPE_DATA,
            ESP_PARTITION_SUBTYPE_DATA_SPIFFS,
            &ext_part
        );
        if (err != ESP_OK || !ext_part) {
            Serial.printf("[FLASH] partition_register: %s\n", esp_err_to_name(err));
            spi_bus_remove_flash_device(ext_flash); ext_flash = nullptr;
            if (attempt < 3) { delay(1000); continue; } break;
        }

        Serial.printf("[FLASH] Partition '%s': size=%uMB match=%s\n",
            ext_part->label, ext_part->size/(1024*1024),
            (ext_part->flash_chip == ext_flash) ? "YES" : "NO-WRONG!");

        // Montuj z etykietą "extfs"
        bool mounted = LittleFS.begin(false, "/littlefs", 10, EXT_FLASH_PARTITION_LABEL);
        if (!mounted) {
            Serial.println("[FLASH] First mount failed - formatting...");
            esp_err_t fmt = esp_littlefs_format(EXT_FLASH_PARTITION_LABEL);
            Serial.printf("[FLASH] Format: %s\n", esp_err_to_name(fmt));
            delay(300);
            mounted = LittleFS.begin(false, "/littlefs", 10, EXT_FLASH_PARTITION_LABEL);
        }

        if (!mounted) {
            Serial.println("[FLASH] Mount FAILED!");
            spi_bus_remove_flash_device(ext_flash); ext_flash = nullptr;
            if (attempt < 3) { delay(1000); continue; } break;
        }

        ext_flash_ready = true;
        size_t total = LittleFS.totalBytes();
        size_t used  = LittleFS.usedBytes();
        Serial.printf("[FLASH] OK! %uMB total, %uKB used, %uKB free\n",
            total/(1024*1024), used/1024, (total-used)/1024);

        if (!LittleFS.exists("/profiles")) {
            File f = LittleFS.open("/profiles/.keep", "w"); if (f) f.close();
        }
        if (!LittleFS.exists("/logs")) {
            File f = LittleFS.open("/logs/.keep", "w"); if (f) f.close();
        }
        if (!LittleFS.exists("/backup")) {
            File f = LittleFS.open("/backup/.keep", "w"); if (f) f.close();
        }

        SPI.begin(18, 19, 23, -1);
        initLoggingSystem();
        log_msg(LOG_LEVEL_INFO, "=== EXTERNAL FLASH INIT COMPLETE ===");
        LOG_FMT(LOG_LEVEL_INFO, "[FLASH] W25Q128 %uMB, %uKB free",
            ext_flash_size_bytes/(1024*1024), (total-used)/1024);
        return;
    }

    SPI.begin(18, 19, 23, -1);
    Serial.println("[FLASH] === INIT FAILED ===");
    log_msg(LOG_LEVEL_ERROR, "External flash init FAILED!");
    if (state_lock()) { g_errorProfile = true; state_unlock(); }
    buzzerBeep(3, 200, 200);
}

// ======================================================
void nvs_init() {
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);
    log_msg(LOG_LEVEL_INFO, "NVS initialized");
}

void hardware_init_wifi() { wifi_init(); }

void initLoggingSystem() {
    File logDir = LittleFS.open("/logs");
    int fileCount = 0;
    if (logDir && logDir.isDirectory()) {
        while (File entry = logDir.openNextFile()) {
            if (!entry.isDirectory()) fileCount++;
            entry.close();
        }
    }
    if (logDir) logDir.close();
    if (fileCount > 10) deleteOldestLog("/logs");

    char filename[32];
    snprintf(filename, sizeof(filename), "/logs/wedzarnia_%lu.log", millis()/1000);
    File f = LittleFS.open(filename, "w");
    if (f) {
        f.println("=== WEDZARNIA LOG START ===");
        f.printf("Timestamp: %lu\n", millis()/1000);
        f.printf("Free heap: %d\n", ESP.getFreeHeap());
        if (ext_flash_size_bytes > 0)
            f.printf("External flash: %u MB (W25Q128)\n", ext_flash_size_bytes/(1024*1024));
        f.close();
        LOG_FMT(LOG_LEVEL_INFO, "Log file created: %s", filename);
    } else {
        log_msg(LOG_LEVEL_ERROR, "Failed to create log file");
    }
}

void deleteOldestLog(const char* dirPath) {
    File dir = LittleFS.open(dirPath);
    if (!dir || !dir.isDirectory()) return;

    char oldestFile[64] = {0};
    unsigned long oldestTime = ULONG_MAX;

    while (File entry = dir.openNextFile()) {
        if (!entry.isDirectory()) {
            const char* fileName = entry.name();
            const char* prefix   = "wedzarnia_";
            const char* baseName = strrchr(fileName, '/');
            if (baseName) baseName++; else baseName = fileName;
            if (strncmp(baseName, prefix, strlen(prefix)) == 0) {
                const char* ts  = baseName + strlen(prefix);
                const char* dot = strrchr(baseName, '.');
                if (dot && dot > ts) {
                    char buf[16]; int len = dot - ts;
                    if (len > 0 && len < 15) {
                        strncpy(buf, ts, len); buf[len] = '\0';
                        unsigned long t = strtoul(buf, NULL, 10);
                        if (t < oldestTime) { oldestTime = t; strncpy(oldestFile, fileName, 63); }
                    }
                }
            }
        }
        entry.close();
    }
    dir.close();

    if (oldestFile[0] && oldestTime != ULONG_MAX) {
        char path[96];
        if (oldestFile[0] == '/') strncpy(path, oldestFile, sizeof(path));
        else snprintf(path, sizeof(path), "%s/%s", dirPath, oldestFile);
        if (LittleFS.remove(path)) LOG_FMT(LOG_LEVEL_INFO, "Deleted old log: %s", path);
    }
}

void logToFile(const String& message) {
    File f = LittleFS.open("/logs/latest.log", "a");
    if (f) { f.printf("[%lu] %s\n", millis()/1000, message.c_str()); f.close(); }
}

void runStartupSelfTest() {
    log_msg(LOG_LEVEL_INFO, "=== STARTUP SELF-TEST ===");

    bool upOk    = digitalRead(PIN_BTN_UP)    == HIGH;
    bool downOk  = digitalRead(PIN_BTN_DOWN)  == HIGH;
    bool enterOk = digitalRead(PIN_BTN_ENTER) == HIGH;
    bool exitOk  = digitalRead(PIN_BTN_EXIT)  == HIGH;
    bool allButtonsOk = upOk && downOk && enterOk && exitOk;

    LOG_FMT(LOG_LEVEL_INFO, "Buttons: UP=%s DOWN=%s ENTER=%s EXIT=%s",
        upOk?"OK":"ERR", downOk?"OK":"ERR", enterOk?"OK":"ERR", exitOk?"OK":"ERR");
    if (!allButtonsOk) log_msg(LOG_LEVEL_WARN, "Button wiring issue detected");

    buzzerBeep(1, 100, 0);

    sensors.requestTemperatures(); delay(1000);
    int cnt = sensors.getDeviceCount();
    bool s1ok = false;
    if (cnt >= 1) {
        double t = sensors.getTempCByIndex(0);
        s1ok = (t != DEVICE_DISCONNECTED_C && t > -20 && t < 100);
        LOG_FMT(s1ok?LOG_LEVEL_INFO:LOG_LEVEL_ERROR, "Sensor 1: %.1f C - %s", t, s1ok?"OK":"FAIL");
    }
    if (cnt >= 2) {
        double t = sensors.getTempCByIndex(1);
        bool ok = (t != DEVICE_DISCONNECTED_C && t > -20 && t < 100);
        LOG_FMT(ok?LOG_LEVEL_INFO:LOG_LEVEL_WARN, "Sensor 2: %.1f C - %s", t, ok?"OK":"WARN");
    }

    testOutput(PIN_SSR1, "Heater 1"); delay(50);
    testOutput(PIN_SSR2, "Heater 2"); delay(50);
    testOutput(PIN_SSR3, "Heater 3"); delay(50);
    testOutput(PIN_FAN,  "Fan");      delay(50);
    allOutputsOff();

    if (ext_flash_ready)
        LOG_FMT(LOG_LEVEL_INFO, "Flash: W25Q128 %uMB OK", ext_flash_size_bytes/(1024*1024));
    else
        log_msg(LOG_LEVEL_WARN, "Flash: NOT READY");

    if (allButtonsOk && s1ok) buzzerBeep(2, 100, 50);
    else                      buzzerBeep(4, 150, 100);

    log_msg(LOG_LEVEL_INFO, "Self-test done");
}

void testOutput(int pin, const char* name) {
    digitalWrite(pin, HIGH); delay(50); digitalWrite(pin, LOW);
    LOG_FMT(LOG_LEVEL_INFO, "%s: OK", name);
}

void testButton(int pin, const char* name) {
    LOG_FMT(LOG_LEVEL_INFO, "%s: %s", name, digitalRead(pin)?"HIGH":"LOW");
}
