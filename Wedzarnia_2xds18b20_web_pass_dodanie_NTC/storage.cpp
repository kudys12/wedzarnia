// storage.cpp - [FIX] snprintf w logach, mniej fragmentacji String
// [NEW]  Funkcje storage_get/save/reset_auth_nvs dla HTTP Basic Auth
#include "storage.h"
#include "config.h"
#include "state.h"
#include <SD.h>
#include <nvs_flash.h>
#include <nvs.h>
#include "FS.h"
#include <WiFi.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include <ArduinoJson.h>

static char lastProfilePath[64] = "/profiles/test.prof";
static char wifiStaSsid[32] = "";
static char wifiStaPass[64] = "";

// [NEW] Bufor dla danych autoryzacji – ładowane z NVS przy starcie
static char authUser[32] = "";
static char authPass[64] = "";

static int backupCounter = 0;
static constexpr int MAX_BACKUPS = 5;

static bool parseBool(const char* s) {
    return (strcmp(s, "1") == 0 || strcasecmp(s, "true") == 0);
}

const char* storage_get_profile_path() { return lastProfilePath; }
const char* storage_get_wifi_ssid()    { return wifiStaSsid; }
const char* storage_get_wifi_pass()    { return wifiStaPass; }

// ======================================================
// [NEW] AUTORYZACJA – gettery
// ======================================================

const char* storage_get_auth_user() {
    // Jeśli bufor pusty (nie załadowany z NVS), zwróć domyślny
    return (authUser[0] != '\0') ? authUser : CFG_AUTH_DEFAULT_USER;
}

const char* storage_get_auth_pass() {
    return (authPass[0] != '\0') ? authPass : CFG_AUTH_DEFAULT_PASS;
}

static bool parseProfileLine(char* line, Step& step) {
    while (*line == ' ' || *line == '\t') line++;

    int len = strlen(line);
    while (len > 0 && (line[len - 1] == '\r' || line[len - 1] == '\n' || line[len - 1] == ' ')) {
        line[--len] = '\0';
    }

    if (len == 0 || line[0] == '#') return false;

    char* fields[10];
    int fieldCount = 0;
    char* token = strtok(line, ";");
    while (token && fieldCount < 10) {
        fields[fieldCount++] = token;
        token = strtok(NULL, ";");
    }

    if (fieldCount < 10) {
        log_msg(LOG_LEVEL_WARN, "Invalid profile line - not enough fields");
        return false;
    }

    strncpy(step.name, fields[0], sizeof(step.name) - 1);
    step.name[sizeof(step.name) - 1] = '\0';
    step.tSet         = constrain(atof(fields[1]), CFG_T_MIN_SET, CFG_T_MAX_SET);
    step.tMeatTarget  = constrain(atof(fields[2]), 0, 100);
    step.minTimeMs    = (unsigned long)(atoi(fields[3])) * 60UL * 1000UL;
    step.powerMode    = constrain(atoi(fields[4]), CFG_POWERMODE_MIN, CFG_POWERMODE_MAX);
    step.smokePwm     = constrain(atoi(fields[5]), CFG_SMOKE_PWM_MIN, CFG_SMOKE_PWM_MAX);
    step.fanMode      = constrain(atoi(fields[6]), 0, 2);
    step.fanOnTime    = max(1000UL, (unsigned long)(atoi(fields[7])) * 1000UL);
    step.fanOffTime   = max(1000UL, (unsigned long)(atoi(fields[8])) * 1000UL);
    step.useMeatTemp  = parseBool(fields[9]);

    return true;
}

bool storage_load_profile() {
    if (strncmp(lastProfilePath, "github:", 7) == 0) {
        return storage_load_github_profile(lastProfilePath + 7);
    } else {
        if (!SD.exists(lastProfilePath)) {
            LOG_FMT(LOG_LEVEL_ERROR, "Profile not found on SD: %s", lastProfilePath);
            if (state_lock()) {
                g_errorProfile = true;
                state_unlock();
            }
            return false;
        }

        storage_backup_config();

        File f = SD.open(lastProfilePath, "r");
        if (!f) {
            log_msg(LOG_LEVEL_ERROR, "Cannot open profile file");
            if (state_lock()) {
                g_errorProfile = true;
                state_unlock();
            }
            return false;
        }

        int loadedStepCount = 0;
        char lineBuf[256];

        while (f.available() && loadedStepCount < MAX_STEPS) {
            int len = f.readBytesUntil('\n', lineBuf, sizeof(lineBuf) - 1);
            lineBuf[len] = '\0';

            if (parseProfileLine(lineBuf, g_profile[loadedStepCount])) {
                loadedStepCount++;
            }
        }
        f.close();

        if (state_lock()) {
            g_stepCount    = loadedStepCount;
            g_errorProfile = (g_stepCount == 0);

            g_processStats.totalProcessTimeSec = 0;
            for (int i = 0; i < g_stepCount; i++) {
                g_processStats.totalProcessTimeSec += g_profile[i].minTimeMs / 1000;
            }

            state_unlock();
        }

        if (g_errorProfile) {
            LOG_FMT(LOG_LEVEL_ERROR, "Failed to load profile: %s", lastProfilePath);
        } else {
            LOG_FMT(LOG_LEVEL_INFO, "Profile loaded from SD: %d steps", g_stepCount);
        }

        return !g_errorProfile;
    }
}

void storage_load_config_nvs() {
    nvs_handle_t nvsHandle;
    if (nvs_open("wedzarnia", NVS_READONLY, &nvsHandle) != ESP_OK) {
        log_msg(LOG_LEVEL_INFO, "No saved config in NVS");
        return;
    }

    size_t len;
    len = sizeof(wifiStaSsid);
    if (nvs_get_str(nvsHandle, "wifi_ssid", wifiStaSsid, &len) != ESP_OK)
        wifiStaSsid[0] = '\0';

    len = sizeof(wifiStaPass);
    if (nvs_get_str(nvsHandle, "wifi_pass", wifiStaPass, &len) != ESP_OK)
        wifiStaPass[0] = '\0';

    len = sizeof(lastProfilePath);
    if (nvs_get_str(nvsHandle, "profile", lastProfilePath, &len) != ESP_OK) {
        strcpy(lastProfilePath, "/profiles/test.prof");
    }

    // [NEW] Wczytaj dane autoryzacji
    len = sizeof(authUser);
    if (nvs_get_str(nvsHandle, "auth_user", authUser, &len) != ESP_OK)
        authUser[0] = '\0';  // puste = użyj domyślnego z config.h

    len = sizeof(authPass);
    if (nvs_get_str(nvsHandle, "auth_pass", authPass, &len) != ESP_OK)
        authPass[0] = '\0';

    if (state_lock()) {
        double tmp_d;
        len = sizeof(tmp_d);
        if (nvs_get_blob(nvsHandle, "manual_tset", &tmp_d, &len) == ESP_OK)
            g_tSet = tmp_d;

        int32_t tmp_i;
        if (nvs_get_i32(nvsHandle, "manual_pow", &tmp_i) == ESP_OK)
            g_powerMode = tmp_i;

        if (nvs_get_i32(nvsHandle, "manual_smoke", &tmp_i) == ESP_OK)
            g_manualSmokePwm = tmp_i;

        if (nvs_get_i32(nvsHandle, "manual_fan", &tmp_i) == ESP_OK)
            g_fanMode = tmp_i;

        state_unlock();
    }

    nvs_close(nvsHandle);
    log_msg(LOG_LEVEL_INFO, "NVS config loaded");
}

static void nvs_save_generic(std::function<void(nvs_handle_t)> action) {
    nvs_handle_t nvsHandle;
    if (nvs_open("wedzarnia", NVS_READWRITE, &nvsHandle) == ESP_OK) {
        action(nvsHandle);
        nvs_commit(nvsHandle);
        nvs_close(nvsHandle);
    }
}

void storage_save_wifi_nvs(const char* ssid, const char* pass) {
    strncpy(wifiStaSsid, ssid, sizeof(wifiStaSsid) - 1);
    wifiStaSsid[sizeof(wifiStaSsid) - 1] = '\0';
    strncpy(wifiStaPass, pass, sizeof(wifiStaPass) - 1);
    wifiStaPass[sizeof(wifiStaPass) - 1] = '\0';

    nvs_save_generic([&](nvs_handle_t handle){
        nvs_set_str(handle, "wifi_ssid", wifiStaSsid);
        nvs_set_str(handle, "wifi_pass", wifiStaPass);
    });

    log_msg(LOG_LEVEL_INFO, "WiFi credentials saved to NVS");
}

void storage_save_profile_path_nvs(const char* path) {
    strncpy(lastProfilePath, path, sizeof(lastProfilePath) - 1);
    lastProfilePath[sizeof(lastProfilePath) - 1] = '\0';

    nvs_save_generic([&](nvs_handle_t handle){
        nvs_set_str(handle, "profile", lastProfilePath);
    });

    LOG_FMT(LOG_LEVEL_INFO, "Profile path saved: %s", path);
}

void storage_save_manual_settings_nvs() {
    if (!state_lock()) return;

    double ts = g_tSet;
    int pm    = g_powerMode;
    int sm    = g_manualSmokePwm;
    int fm    = g_fanMode;
    state_unlock();

    nvs_save_generic([=](nvs_handle_t handle){
        nvs_set_blob(handle, "manual_tset", &ts, sizeof(ts));
        nvs_set_i32(handle, "manual_pow",   pm);
        nvs_set_i32(handle, "manual_smoke", sm);
        nvs_set_i32(handle, "manual_fan",   fm);
    });

    log_msg(LOG_LEVEL_DEBUG, "Manual settings saved to NVS");
}

// ======================================================
// [NEW] AUTORYZACJA – zapis i reset w NVS
// ======================================================

void storage_save_auth_nvs(const char* user, const char* pass) {
    // Walidacja długości
    if (strlen(user) == 0 || strlen(user) >= sizeof(authUser) ||
        strlen(pass) == 0 || strlen(pass) >= sizeof(authPass)) {
        log_msg(LOG_LEVEL_ERROR, "Auth save failed: invalid user/pass length");
        return;
    }

    strncpy(authUser, user, sizeof(authUser) - 1);
    authUser[sizeof(authUser) - 1] = '\0';
    strncpy(authPass, pass, sizeof(authPass) - 1);
    authPass[sizeof(authPass) - 1] = '\0';

    nvs_save_generic([&](nvs_handle_t handle){
        nvs_set_str(handle, "auth_user", authUser);
        nvs_set_str(handle, "auth_pass", authPass);
    });

    log_msg(LOG_LEVEL_INFO, "Auth credentials saved to NVS");
}

void storage_reset_auth_nvs() {
    // Wyczyść bufory – storage_get_auth_*() wróci do domyślnych z config.h
    authUser[0] = '\0';
    authPass[0] = '\0';

    nvs_handle_t nvsHandle;
    if (nvs_open("wedzarnia", NVS_READWRITE, &nvsHandle) == ESP_OK) {
        nvs_erase_key(nvsHandle, "auth_user");
        nvs_erase_key(nvsHandle, "auth_pass");
        nvs_commit(nvsHandle);
        nvs_close(nvsHandle);
    }

    LOG_FMT(LOG_LEVEL_INFO, "Auth reset to defaults (user=%s)", CFG_AUTH_DEFAULT_USER);
}

// ======================================================
// PROFILES JSON
// ======================================================

String storage_list_profiles_json() {
    char json[512];
    int offset = 0;
    offset += snprintf(json + offset, sizeof(json) - offset, "[");

    File root = SD.open("/profiles");
    if (!root || !root.isDirectory()) {
        log_msg(LOG_LEVEL_WARN, "Cannot open /profiles directory");
        return "[]";
    }

    bool first = true;
    File file = root.openNextFile();
    while (file) {
        if (!file.isDirectory()) {
            const char* fileName = file.name();
            int nameLen = strlen(fileName);
            if (nameLen > 5 && strcmp(fileName + nameLen - 5, ".prof") == 0) {
                if (!first) {
                    offset += snprintf(json + offset, sizeof(json) - offset, ",");
                }
                offset += snprintf(json + offset, sizeof(json) - offset, "\"%s\"", fileName);
                first = false;
                if (offset >= (int)sizeof(json) - 20) break;
            }
        }
        file = root.openNextFile();
    }
    root.close();
    snprintf(json + offset, sizeof(json) - offset, "]");

    return String(json);
}

bool storage_reinit_sd() {
    log_msg(LOG_LEVEL_INFO, "Re-initializing SD card...");
    SD.end();
    delay(200);

    if (SD.begin(PIN_SD_CS)) {
        log_msg(LOG_LEVEL_INFO, "SD card re-initialized successfully");
        return true;
    } else {
        log_msg(LOG_LEVEL_ERROR, "Failed to re-initialize SD card");
        return false;
    }
}

String storage_get_profile_as_json(const char* profileName) {
    char path[96];
    snprintf(path, sizeof(path), "/profiles/%s", profileName);

    if (!SD.exists(path)) {
        LOG_FMT(LOG_LEVEL_WARN, "Profile not found: %s", path);
        return "[]";
    }

    File f = SD.open(path, "r");
    if (!f) {
        LOG_FMT(LOG_LEVEL_ERROR, "Cannot open profile: %s", path);
        return "[]";
    }

    char json[2048];
    int offset = 0;
    offset += snprintf(json + offset, sizeof(json) - offset, "[");
    bool firstStep = true;
    char lineBuf[256];

    while (f.available()) {
        int len = f.readBytesUntil('\n', lineBuf, sizeof(lineBuf) - 1);
        lineBuf[len] = '\0';

        char* line = lineBuf;
        while (*line == ' ' || *line == '\t') line++;
        len = strlen(line);
        while (len > 0 && (line[len - 1] == '\r' || line[len - 1] == '\n' || line[len - 1] == ' ')) {
            line[--len] = '\0';
        }
        if (len == 0 || line[0] == '#') continue;

        char lineCopy[256];
        strncpy(lineCopy, line, sizeof(lineCopy));
        lineCopy[sizeof(lineCopy) - 1] = '\0';

        char* fields[10];
        int fieldCount = 0;
        char* token = strtok(lineCopy, ";");
        while (token && fieldCount < 10) {
            fields[fieldCount++] = token;
            token = strtok(NULL, ";");
        }
        if (fieldCount < 10) continue;

        if (!firstStep) {
            offset += snprintf(json + offset, sizeof(json) - offset, ",");
        }
        offset += snprintf(json + offset, sizeof(json) - offset,
            "{\"name\":\"%s\",\"tSet\":%s,\"tMeat\":%s,\"minTime\":%s,"
            "\"powerMode\":%s,\"smoke\":%s,\"fanMode\":%s,"
            "\"fanOn\":%s,\"fanOff\":%s,\"useMeatTemp\":%s}",
            fields[0], fields[1], fields[2], fields[3],
            fields[4], fields[5], fields[6],
            fields[7], fields[8], fields[9]);
        firstStep = false;

        if (offset >= (int)sizeof(json) - 50) break;
    }
    f.close();
    snprintf(json + offset, sizeof(json) - offset, "]");

    return String(json);
}

String storage_list_github_profiles_json() {
    if (WiFi.status() != WL_CONNECTED) {
        log_msg(LOG_LEVEL_WARN, "WiFi not connected - cannot list GitHub profiles");
        return "[\"Brak WiFi\"]";
    }

    HTTPClient http;
    WiFiClientSecure secureClient;
    secureClient.setInsecure();

    http.begin(secureClient, CFG_GITHUB_API_URL);
    http.addHeader("User-Agent", "ESP32-Wedzarnia/3.4");
    http.setTimeout(10000);
    http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
    int httpCode = http.GET();

    if (httpCode != HTTP_CODE_OK) {
        LOG_FMT(LOG_LEVEL_ERROR, "GitHub API list error: %d", httpCode);
        http.end();
        return "[\"Blad API GitHub\"]";
    }

    // getString() zamiast getStream() – niezawodne dla HTTPS na ESP32
    String responseBody = http.getString();
    http.end();

    // Zwiększony bufor – API GitHub zwraca ~300 bajtów na plik
    StaticJsonDocument<4096> doc;
    DeserializationError error = deserializeJson(doc, responseBody);

    if (error) {
        LOG_FMT(LOG_LEVEL_ERROR, "GitHub JSON parse error: %s", error.c_str());
        return "[\"Blad parsowania\"]";
    }

    char json[512];
    int offset = 0;
    offset += snprintf(json + offset, sizeof(json) - offset, "[");
    bool first = true;

    for (JsonVariant value : doc.as<JsonArray>()) {
        const char* filename = value["name"];
        if (!filename) continue;
        int nameLen = strlen(filename);
        if (nameLen > 5 && strcmp(filename + nameLen - 5, ".prof") == 0) {
            if (!first) {
                offset += snprintf(json + offset, sizeof(json) - offset, ",");
            }
            offset += snprintf(json + offset, sizeof(json) - offset, "\"%s\"", filename);
            first = false;
            if (offset >= (int)sizeof(json) - 50) break;
        }
    }
    snprintf(json + offset, sizeof(json) - offset, "]");

    return String(json);
}

bool storage_load_github_profile(const char* profileName) {
    if (WiFi.status() != WL_CONNECTED) {
        LOG_FMT(LOG_LEVEL_ERROR, "WiFi not connected - cannot load from GitHub");
        if (state_lock()) { g_errorProfile = true; state_unlock(); }
        return false;
    }

    HTTPClient http;
    // raw.githubusercontent.com wymaga HTTPS – użyj WiFiClientSecure
    WiFiClientSecure secureClient;
    secureClient.setInsecure(); // pomijamy weryfikację certyfikatu (brak CA store na ESP32)

    char url[192];
    snprintf(url, sizeof(url), "%s%s", CFG_GITHUB_PROFILES_BASE_URL, profileName);
    LOG_FMT(LOG_LEVEL_INFO, "Loading GitHub profile: %s", url);

    http.begin(secureClient, url);
    http.setTimeout(15000);
    http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
    http.addHeader("User-Agent", "ESP32-Wedzarnia/3.4");
    http.addHeader("Accept",     "text/plain");

    int httpCode = http.GET();

    if (httpCode != HTTP_CODE_OK) {
        LOG_FMT(LOG_LEVEL_ERROR, "GitHub GET failed HTTP %d: %s", httpCode, url);
        http.end();
        if (state_lock()) { g_errorProfile = true; state_unlock(); }
        return false;
    }

    // WAŻNE: stream->available() na ESP32+HTTPS często zwraca 0 mimo że dane
    // jeszcze przychodzą (znany bug biblioteki). getString() czeka na pełną
    // odpowiedź i jest niezawodne dla małych plików (<10 kB jak nasze profile).
    String body = http.getString();
    http.end();

    if (body.length() == 0) {
        LOG_FMT(LOG_LEVEL_ERROR, "Empty response from GitHub for: %s", profileName);
        if (state_lock()) { g_errorProfile = true; state_unlock(); }
        return false;
    }

    LOG_FMT(LOG_LEVEL_DEBUG, "GitHub body: %d bytes", body.length());

    // Parsuj linia po linii z String zamiast ze streamu
    int loadedStepCount = 0;
    int pos = 0;
    int bodyLen = body.length();

    while (pos < bodyLen && loadedStepCount < MAX_STEPS) {
        int eol = body.indexOf('\n', pos);
        if (eol < 0) eol = bodyLen;

        char lineBuf[256];
        int lineLen = min((int)(eol - pos), (int)(sizeof(lineBuf) - 1));
        body.substring(pos, pos + lineLen).toCharArray(lineBuf, sizeof(lineBuf));
        lineBuf[lineLen] = '\0';

        if (parseProfileLine(lineBuf, g_profile[loadedStepCount])) {
            loadedStepCount++;
        }
        pos = eol + 1;
    }

    if (state_lock()) {
        g_stepCount    = loadedStepCount;
        g_errorProfile = (g_stepCount == 0);

        g_processStats.totalProcessTimeSec = 0;
        for (int i = 0; i < g_stepCount; i++) {
            g_processStats.totalProcessTimeSec += g_profile[i].minTimeMs / 1000;
        }
        state_unlock();
    }

    if (g_errorProfile) {
        LOG_FMT(LOG_LEVEL_ERROR, "No valid steps in GitHub profile: %s", profileName);
    } else {
        LOG_FMT(LOG_LEVEL_INFO, "GitHub profile '%s' OK: %d steps", profileName, g_stepCount);
    }

    return !g_errorProfile;
}

void storage_backup_config() {
    backupCounter++;
    if (backupCounter % 5 != 0) return;

    char backupPath[64];
    snprintf(backupPath, sizeof(backupPath), "/backup/config_%lu.bak", millis() / 1000);

    if (!SD.exists("/backup")) {
        if (!SD.mkdir("/backup")) {
            log_msg(LOG_LEVEL_ERROR, "Failed to create backup directory");
            return;
        }
    }

    File backupFile = SD.open(backupPath, FILE_WRITE);
    if (!backupFile) {
        log_msg(LOG_LEVEL_ERROR, "Failed to create backup file");
        return;
    }

    StaticJsonDocument<512> doc;
    doc["profile_path"]      = lastProfilePath;
    doc["wifi_ssid"]         = wifiStaSsid;
    doc["backup_timestamp"]  = millis() / 1000;

    serializeJson(doc, backupFile);
    backupFile.close();

    LOG_FMT(LOG_LEVEL_INFO, "Config backup created: %s", backupPath);

    cleanupOldBackups();
}

void cleanupOldBackups() {
    File backupDir = SD.open("/backup");
    if (!backupDir) return;

    char backupFiles[MAX_BACKUPS + 5][64];
    int fileCount = 0;

    while (File entry = backupDir.openNextFile()) {
        if (!entry.isDirectory()) {
            const char* fileName = entry.name();
            int nameLen = strlen(fileName);
            if (nameLen > 4 && strcmp(fileName + nameLen - 4, ".bak") == 0) {
                if (fileCount < (int)(sizeof(backupFiles) / sizeof(backupFiles[0]))) {
                    strncpy(backupFiles[fileCount], fileName, sizeof(backupFiles[0]) - 1);
                    backupFiles[fileCount][sizeof(backupFiles[0]) - 1] = '\0';
                    fileCount++;
                }
            }
        }
        entry.close();
    }
    backupDir.close();

    if (fileCount > MAX_BACKUPS) {
        for (int i = 0; i < fileCount - 1; i++) {
            for (int j = 0; j < fileCount - i - 1; j++) {
                if (strcmp(backupFiles[j], backupFiles[j+1]) > 0) {
                    char temp[64];
                    strcpy(temp, backupFiles[j]);
                    strcpy(backupFiles[j], backupFiles[j+1]);
                    strcpy(backupFiles[j+1], temp);
                }
            }
        }

        int filesToDelete = fileCount - MAX_BACKUPS;
        for (int i = 0; i < filesToDelete; i++) {
            if (SD.remove(backupFiles[i])) {
                LOG_FMT(LOG_LEVEL_INFO, "Deleted old backup: %s", backupFiles[i]);
            }
        }
    }
}

bool storage_restore_backup(const char* backupPath) {
    if (!SD.exists(backupPath)) {
        LOG_FMT(LOG_LEVEL_ERROR, "Backup file not found: %s", backupPath);
        return false;
    }

    File backupFile = SD.open(backupPath, "r");
    if (!backupFile) {
        log_msg(LOG_LEVEL_ERROR, "Cannot open backup file");
        return false;
    }

    StaticJsonDocument<512> doc;
    DeserializationError error = deserializeJson(doc, backupFile);
    backupFile.close();

    if (error) {
        LOG_FMT(LOG_LEVEL_ERROR, "Failed to parse backup JSON: %s", error.c_str());
        return false;
    }

    const char* profilePath = doc["profile_path"];
    const char* wifiSsid    = doc["wifi_ssid"];
    unsigned long timestamp = doc["backup_timestamp"];

    if (profilePath) {
        strncpy(lastProfilePath, profilePath, sizeof(lastProfilePath) - 1);
        LOG_FMT(LOG_LEVEL_INFO, "Restored profile path: %s", profilePath);
    }

    if (wifiSsid && strlen(wifiSsid) > 0) {
        strncpy(wifiStaSsid, wifiSsid, sizeof(wifiStaSsid) - 1);
        LOG_FMT(LOG_LEVEL_INFO, "Restored WiFi SSID: %s", wifiSsid);
    }

    storage_save_profile_path_nvs(lastProfilePath);
    storage_save_wifi_nvs(wifiStaSsid, wifiStaPass);

    LOG_FMT(LOG_LEVEL_INFO, "Backup restored (timestamp: %lu)", timestamp);
    return true;
}

String storage_list_backups_json() {
    if (!SD.exists("/backup")) {
        return "[]";
    }

    File backupDir = SD.open("/backup");
    if (!backupDir || !backupDir.isDirectory()) {
        return "[]";
    }

    char json[512];
    int offset = 0;
    offset += snprintf(json + offset, sizeof(json) - offset, "[");
    bool first = true;

    while (File entry = backupDir.openNextFile()) {
        if (!entry.isDirectory()) {
            const char* fileName = entry.name();
            int nameLen = strlen(fileName);
            if (nameLen > 4 && strcmp(fileName + nameLen - 4, ".bak") == 0) {
                if (!first) {
                    offset += snprintf(json + offset, sizeof(json) - offset, ",");
                }
                offset += snprintf(json + offset, sizeof(json) - offset, "\"%s\"", fileName);
                first = false;
                if (offset >= (int)sizeof(json) - 50) break;
            }
        }
        entry.close();
    }
    backupDir.close();
    snprintf(json + offset, sizeof(json) - offset, "]");

    return String(json);
}