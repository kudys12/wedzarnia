// hardware.cpp - FIXED compilation error
// Zmiana: gpio_set_direction/gpio_set_level (ESP-IDF) -> pinMode/digitalWrite (Arduino)
// Dodano: #include <driver/gpio.h> nie jest potrzebny - używamy Arduino API

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

static esp_flash_t* ext_flash            = nullptr;
static uint32_t     ext_flash_size_bytes = 0;
static bool         ext_flash_ready      = false;

uint32_t hardware_get_ext_flash_size()       { return ext_flash_size_bytes; }
esp_flash_t* hardware_get_ext_flash_handle() { return ext_flash; }

bool hardware_remount_flash(bool format) {
    if (!ext_flash || ext_flash_size_bytes == 0) {
        Serial.println("[FLASH] remount: ext_flash not initialized!");
        return false;
    }

    const esp_partition_t* part = esp_partition_find_first(
        ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_DATA_SPIFFS, EXT_FLASH_PARTITION_LABEL);

    if (!part) {
        Serial.println("[FLASH] remount: re-registering partition...");
        esp_err_t err = esp_partition_register_external(
            ext_flash, 0, ext_flash_size_bytes, EXT_FLASH_PARTITION_LABEL,
            ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_DATA_SPIFFS, &part);
        if (err != ESP_OK || !part) {
            Serial.printf("[FLASH] remount register failed: %s\n", esp_err_to_name(err));
            return false;
        }
    }

    if (format) {
        esp_err_t fmt = esp_littlefs_format(EXT_FLASH_PARTITION_LABEL);
        Serial.printf("[FLASH] remount format: %s\n", esp_err_to_name(fmt));
        if (fmt != ESP_OK) return false;
        delay(200);
    }

    bool ok = LittleFS.begin(false, "/littlefs", 10, EXT_FLASH_PARTITION_LABEL);
    if (!ok && !format) {
        esp_littlefs_format(EXT_FLASH_PARTITION_LABEL);
        delay(200);
        ok = LittleFS.begin(false, "/littlefs", 10, EXT_FLASH_PARTITION_LABEL);
    }

    if (ok) {
        if (!LittleFS.exists("/profiles")) { File f=LittleFS.open("/profiles/.keep","w"); if(f)f.close(); }
        if (!LittleFS.exists("/logs"))     { File f=LittleFS.open("/logs/.keep","w");     if(f)f.close(); }
        if (!LittleFS.exists("/backup"))   { File f=LittleFS.open("/backup/.keep","w");   if(f)f.close(); }

        size_t tot=0, use=0;
        esp_littlefs_info(EXT_FLASH_PARTITION_LABEL, &tot, &use);
        Serial.printf("[FLASH] remount OK: %uMB total, %uKB free\n",
            (unsigned)(tot/(1024*1024)), (unsigned)((tot-use)/1024));
    } else {
        Serial.println("[FLASH] remount: FAILED!");
    }
    return ok;
}

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
    if (!ledcAttach(PIN_SSR1,      LEDC_FREQ, LEDC_RESOLUTION)) { log_msg(LOG_LEVEL_ERROR, "LEDC SSR1!");  ok=false; }
    if (!ledcAttach(PIN_SSR2,      LEDC_FREQ, LEDC_RESOLUTION)) { log_msg(LOG_LEVEL_ERROR, "LEDC SSR2!");  ok=false; }
    if (!ledcAttach(PIN_SSR3,      LEDC_FREQ, LEDC_RESOLUTION)) { log_msg(LOG_LEVEL_ERROR, "LEDC SSR3!");  ok=false; }
    if (!ledcAttach(PIN_SMOKE_FAN, LEDC_FREQ, LEDC_RESOLUTION)) { log_msg(LOG_LEVEL_ERROR, "LEDC SMOKE!"); ok=false; }
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
    display.setTextSize(2); display.println("WEDZARNIA");
    display.setTextSize(1); display.println("\n   " FW_VERSION);
    display.println("   by " FW_AUTHOR);
    display.println("\n   Inicjalizacja...");
    delay(1500);
    log_msg(LOG_LEVEL_INFO, "Display initialized");
}

void hardware_init_flash() {
    esp_task_wdt_reset();
    Serial.println("[FLASH] === INIT START ===");

    // POPRAWKA: pinMode/digitalWrite zamiast gpio_set_direction/gpio_set_level
    // gpio_set_direction wymagałoby #include <driver/gpio.h> z ESP-IDF
    // pinMode/digitalWrite jest zawsze dostępne w Arduino ESP32
    pinMode(TFT_CS,      OUTPUT); digitalWrite(TFT_CS,      HIGH);
    pinMode(PIN_FLASH_CS, OUTPUT); digitalWrite(PIN_FLASH_CS, HIGH);
    delay(100);

    SPI.end();
    delay(100);

    spi_bus_config_t bus_cfg = {};
    bus_cfg.mosi_io_num = 23; bus_cfg.miso_io_num = 19; bus_cfg.sclk_io_num = 18;
    bus_cfg.quadwp_io_num = -1; bus_cfg.quadhd_io_num = -1; bus_cfg.max_transfer_sz = 4096;

    esp_err_t err = spi_bus_initialize(SPI2_HOST, &bus_cfg, SPI_DMA_CH_AUTO);
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
        Serial.printf("[FLASH] SPI2 FAILED: %s\n", esp_err_to_name(err));
        SPI.begin(18, 19, 23, -1);
        if (state_lock()) { g_errorProfile = true; state_unlock(); }
        buzzerBeep(3, 200, 200); return;
    }
    Serial.printf("[FLASH] SPI2: %s\n", esp_err_to_name(err));

    esp_flash_spi_device_config_t dev_cfg = {};
    dev_cfg.host_id = SPI2_HOST; dev_cfg.cs_id = 0; dev_cfg.cs_io_num = PIN_FLASH_CS;
    dev_cfg.io_mode = SPI_FLASH_SLOWRD; dev_cfg.freq_mhz = 20; dev_cfg.input_delay_ns = 0;

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

        uint32_t jedec = 0; esp_flash_read_id(ext_flash, &jedec);
        Serial.printf("[FLASH] JEDEC=0x%06X  Size=%uMB\n", jedec, ext_flash_size_bytes/(1024*1024));

        const esp_partition_t* ext_part = nullptr;
        err = esp_partition_register_external(
            ext_flash, 0, ext_flash_size_bytes, EXT_FLASH_PARTITION_LABEL,
            ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_DATA_SPIFFS, &ext_part);
        if (err != ESP_OK || !ext_part) {
            Serial.printf("[FLASH] partition_register: %s\n", esp_err_to_name(err));
            spi_bus_remove_flash_device(ext_flash); ext_flash = nullptr;
            if (attempt < 3) { delay(1000); continue; } break;
        }
        Serial.printf("[FLASH] Partition '%s': %uMB chip_match=%s\n",
            ext_part->label, ext_part->size/(1024*1024),
            (ext_part->flash_chip == ext_flash) ? "YES" : "NO!");

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

        // Weryfikacja przez esp_littlefs_info (nie LittleFS.totalBytes() które może = 0)
        size_t total=0, used=0;
        esp_littlefs_info(EXT_FLASH_PARTITION_LABEL, &total, &used);
        Serial.printf("[FLASH] OK! esp_littlefs_info: %uMB total, %uKB used, %uKB free\n",
            (unsigned)(total/(1024*1024)), (unsigned)(used/1024), (unsigned)((total-used)/1024));

        if (!LittleFS.exists("/profiles")) { File f=LittleFS.open("/profiles/.keep","w"); if(f)f.close(); }
        if (!LittleFS.exists("/logs"))     { File f=LittleFS.open("/logs/.keep","w");     if(f)f.close(); }
        if (!LittleFS.exists("/backup"))   { File f=LittleFS.open("/backup/.keep","w");   if(f)f.close(); }

        SPI.begin(18, 19, 23, -1);
        initLoggingSystem();
        log_msg(LOG_LEVEL_INFO, "=== EXTERNAL FLASH INIT COMPLETE ===");
        LOG_FMT(LOG_LEVEL_INFO, "[FLASH] W25Q128 %uMB OK", ext_flash_size_bytes/(1024*1024));
        return;
    }

    SPI.begin(18, 19, 23, -1);
    Serial.println("[FLASH] === INIT FAILED ===");
    log_msg(LOG_LEVEL_ERROR, "External flash FAILED!");
    if (state_lock()) { g_errorProfile = true; state_unlock(); }
    buzzerBeep(3, 200, 200);
}

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
    int cnt = 0;
    if (logDir && logDir.isDirectory()) {
        while (File e = logDir.openNextFile()) { if (!e.isDirectory()) cnt++; e.close(); }
    }
    if (logDir) logDir.close();
    if (cnt > 10) deleteOldestLog("/logs");

    char fn[48];
    snprintf(fn, sizeof(fn), "/logs/wedzarnia_%lu.log", millis()/1000);
    File f = LittleFS.open(fn, "w");
    if (f) {
        f.println("=== WEDZARNIA LOG ===");
        f.printf("Time: %lu\n", millis()/1000);
        f.printf("Heap: %d\n", ESP.getFreeHeap());
        if (ext_flash_size_bytes > 0) f.printf("W25Q128: %uMB\n", ext_flash_size_bytes/(1024*1024));
        f.close();
        LOG_FMT(LOG_LEVEL_INFO, "Log: %s", fn);
    } else {
        log_msg(LOG_LEVEL_ERROR, "Log create failed");
    }
}

void deleteOldestLog(const char* dirPath) {
    File dir = LittleFS.open(dirPath);
    if (!dir || !dir.isDirectory()) return;
    char oldest[64] = {}; unsigned long oldTime = ULONG_MAX;
    while (File e = dir.openNextFile()) {
        if (!e.isDirectory()) {
            const char* fn = e.name();
            const char* base = strrchr(fn, '/'); base = base ? base+1 : fn;
            if (strncmp(base, "wedzarnia_", 10) == 0) {
                const char* ts = base+10; const char* dot = strrchr(base, '.');
                if (dot && dot > ts) {
                    char buf[16]; int l = dot-ts;
                    if (l>0 && l<15) { strncpy(buf,ts,l); buf[l]='\0';
                        unsigned long t = strtoul(buf,NULL,10);
                        if (t < oldTime) { oldTime=t; strncpy(oldest,fn,63); }
                    }
                }
            }
        }
        e.close();
    }
    dir.close();
    if (oldest[0] && oldTime != ULONG_MAX) {
        char path[96];
        if (oldest[0]=='/') strncpy(path,oldest,sizeof(path));
        else snprintf(path,sizeof(path),"%s/%s",dirPath,oldest);
        if (LittleFS.remove(path)) LOG_FMT(LOG_LEVEL_INFO,"Deleted log: %s",path);
    }
}

void logToFile(const String& msg) {
    File f = LittleFS.open("/logs/latest.log","a");
    if (f) { f.printf("[%lu] %s\n", millis()/1000, msg.c_str()); f.close(); }
}

void runStartupSelfTest() {
    log_msg(LOG_LEVEL_INFO, "=== SELF-TEST ===");
    bool upOk=digitalRead(PIN_BTN_UP)==HIGH, downOk=digitalRead(PIN_BTN_DOWN)==HIGH;
    bool entOk=digitalRead(PIN_BTN_ENTER)==HIGH, exitOk=digitalRead(PIN_BTN_EXIT)==HIGH;
    LOG_FMT(LOG_LEVEL_INFO,"Buttons: UP=%s DN=%s EN=%s EX=%s",
        upOk?"OK":"ERR",downOk?"OK":"ERR",entOk?"OK":"ERR",exitOk?"OK":"ERR");
    buzzerBeep(1,100,0);

    sensors.requestTemperatures(); delay(1000);
    int cnt = sensors.getDeviceCount(); bool s1ok=false;
    if (cnt>=1) {
        double t=sensors.getTempCByIndex(0); s1ok=(t!=DEVICE_DISCONNECTED_C&&t>-20&&t<100);
        LOG_FMT(s1ok?LOG_LEVEL_INFO:LOG_LEVEL_ERROR,"Sensor1: %.1fC %s",t,s1ok?"OK":"FAIL");
    }
    if (cnt>=2) {
        double t=sensors.getTempCByIndex(1); bool ok=(t!=DEVICE_DISCONNECTED_C&&t>-20&&t<100);
        LOG_FMT(ok?LOG_LEVEL_INFO:LOG_LEVEL_WARN,"Sensor2: %.1fC %s",t,ok?"OK":"WARN");
    }

    testOutput(PIN_SSR1,"Heater1"); delay(50);
    testOutput(PIN_SSR2,"Heater2"); delay(50);
    testOutput(PIN_SSR3,"Heater3"); delay(50);
    testOutput(PIN_FAN, "Fan");     delay(50);
    allOutputsOff();

    if (ext_flash_ready) LOG_FMT(LOG_LEVEL_INFO,"Flash: W25Q128 %uMB OK",ext_flash_size_bytes/(1024*1024));
    else                 log_msg(LOG_LEVEL_WARN,"Flash: NOT READY");

    if ((upOk&&downOk&&entOk&&exitOk)&&s1ok) buzzerBeep(2,100,50);
    else buzzerBeep(4,150,100);
    log_msg(LOG_LEVEL_INFO,"Self-test done");
}

void testOutput(int pin, const char* name) {
    digitalWrite(pin,HIGH); delay(50); digitalWrite(pin,LOW);
    LOG_FMT(LOG_LEVEL_INFO,"%s: OK",name);
}

void testButton(int pin, const char* name) {
    LOG_FMT(LOG_LEVEL_INFO,"%s: %s",name,digitalRead(pin)?"HIGH":"LOW");
}
