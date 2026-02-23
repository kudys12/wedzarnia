// storage.cpp
// [FIX] storage_reinit_flash() używa hardware_remount_flash() zamiast
//       LittleFS.begin(true) który montował WEWNĘTRZNY flash zamiast W25Q128

#include "storage.h"
#include "config.h"
#include "state.h"
#include "hardware.h"
#include <LittleFS.h>
#include <nvs_flash.h>
#include <nvs.h>
#include "FS.h"
#include <WiFi.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include <ArduinoJson.h>

static char lastProfilePath[64] = "/profiles/test.prof";
static char wifiStaSsid[32]     = "";
static char wifiStaPass[64]     = "";
static char authUser[32]        = "";
static char authPass[64]        = "";
static int  backupCounter       = 0;
static constexpr int MAX_BACKUPS = 5;

static bool parseBool(const char* s) {
    return (strcmp(s, "1") == 0 || strcasecmp(s, "true") == 0);
}

const char* storage_get_profile_path() { return lastProfilePath; }
const char* storage_get_wifi_ssid()    { return wifiStaSsid; }
const char* storage_get_wifi_pass()    { return wifiStaPass; }

const char* storage_get_auth_user() {
    return (authUser[0] != '\0') ? authUser : CFG_AUTH_DEFAULT_USER;
}
const char* storage_get_auth_pass() {
    return (authPass[0] != '\0') ? authPass : CFG_AUTH_DEFAULT_PASS;
}

static bool parseProfileLine(char* line, Step& step) {
    while (*line == ' ' || *line == '\t') line++;
    int len = strlen(line);
    while (len > 0 && (line[len-1] == '\r' || line[len-1] == '\n' || line[len-1] == ' '))
        line[--len] = '\0';
    if (len == 0 || line[0] == '#') return false;

    char* fields[10];
    int fieldCount = 0;
    char* token = strtok(line, ";");
    while (token && fieldCount < 10) { fields[fieldCount++] = token; token = strtok(NULL, ";"); }
    if (fieldCount < 10) { log_msg(LOG_LEVEL_WARN, "Invalid profile line"); return false; }

    strncpy(step.name, fields[0], sizeof(step.name)-1);
    step.name[sizeof(step.name)-1] = '\0';
    step.tSet        = constrain(atof(fields[1]), CFG_T_MIN_SET, CFG_T_MAX_SET);
    step.tMeatTarget = constrain(atof(fields[2]), 0, 100);
    step.minTimeMs   = (unsigned long)(atoi(fields[3])) * 60UL * 1000UL;
    step.powerMode   = constrain(atoi(fields[4]), CFG_POWERMODE_MIN, CFG_POWERMODE_MAX);
    step.smokePwm    = constrain(atoi(fields[5]), CFG_SMOKE_PWM_MIN, CFG_SMOKE_PWM_MAX);
    step.fanMode     = constrain(atoi(fields[6]), 0, 2);
    step.fanOnTime   = max(1000UL, (unsigned long)(atoi(fields[7])) * 1000UL);
    step.fanOffTime  = max(1000UL, (unsigned long)(atoi(fields[8])) * 1000UL);
    step.useMeatTemp = parseBool(fields[9]);
    return true;
}

bool storage_load_profile() {
    if (strncmp(lastProfilePath, "github:", 7) == 0) {
        return storage_load_github_profile(lastProfilePath + 7);
    }

    if (!LittleFS.exists(lastProfilePath)) {
        LOG_FMT(LOG_LEVEL_ERROR, "Profile not found: %s", lastProfilePath);
        if (state_lock()) { g_errorProfile = true; state_unlock(); }
        return false;
    }

    storage_backup_config();

    File f = LittleFS.open(lastProfilePath, "r");
    if (!f) {
        log_msg(LOG_LEVEL_ERROR, "Cannot open profile file");
        if (state_lock()) { g_errorProfile = true; state_unlock(); }
        return false;
    }

    int loadedStepCount = 0;
    char lineBuf[256];
    while (f.available() && loadedStepCount < MAX_STEPS) {
        int len = f.readBytesUntil('\n', lineBuf, sizeof(lineBuf)-1);
        lineBuf[len] = '\0';
        if (parseProfileLine(lineBuf, g_profile[loadedStepCount])) loadedStepCount++;
    }
    f.close();

    if (state_lock()) {
        g_stepCount    = loadedStepCount;
        g_errorProfile = (g_stepCount == 0);
        g_processStats.totalProcessTimeSec = 0;
        for (int i = 0; i < g_stepCount; i++)
            g_processStats.totalProcessTimeSec += g_profile[i].minTimeMs / 1000;
        state_unlock();
    }

    if (g_errorProfile) LOG_FMT(LOG_LEVEL_ERROR, "Profile load failed: %s", lastProfilePath);
    else                LOG_FMT(LOG_LEVEL_INFO,  "Profile loaded: %d steps", g_stepCount);
    return !g_errorProfile;
}

void storage_load_config_nvs() {
    nvs_handle_t h;
    if (nvs_open("wedzarnia", NVS_READONLY, &h) != ESP_OK) {
        log_msg(LOG_LEVEL_INFO, "No saved config in NVS");
        return;
    }

    size_t len;
    len = sizeof(wifiStaSsid);  if (nvs_get_str(h, "wifi_ssid", wifiStaSsid, &len) != ESP_OK) wifiStaSsid[0] = '\0';
    len = sizeof(wifiStaPass);  if (nvs_get_str(h, "wifi_pass", wifiStaPass, &len) != ESP_OK) wifiStaPass[0] = '\0';
    len = sizeof(lastProfilePath); if (nvs_get_str(h, "profile", lastProfilePath, &len) != ESP_OK) strcpy(lastProfilePath, "/profiles/test.prof");
    len = sizeof(authUser);     if (nvs_get_str(h, "auth_user", authUser, &len) != ESP_OK) authUser[0] = '\0';
    len = sizeof(authPass);     if (nvs_get_str(h, "auth_pass", authPass, &len) != ESP_OK) authPass[0] = '\0';

    if (state_lock()) {
        double tmp_d; len = sizeof(tmp_d);
        if (nvs_get_blob(h, "manual_tset", &tmp_d, &len) == ESP_OK) g_tSet = tmp_d;
        int32_t tmp_i;
        if (nvs_get_i32(h, "manual_pow",   &tmp_i) == ESP_OK) g_powerMode      = tmp_i;
        if (nvs_get_i32(h, "manual_smoke", &tmp_i) == ESP_OK) g_manualSmokePwm = tmp_i;
        if (nvs_get_i32(h, "manual_fan",   &tmp_i) == ESP_OK) g_fanMode        = tmp_i;
        state_unlock();
    }

    nvs_close(h);
    log_msg(LOG_LEVEL_INFO, "NVS config loaded");
}

static void nvs_save_generic(std::function<void(nvs_handle_t)> action) {
    nvs_handle_t h;
    if (nvs_open("wedzarnia", NVS_READWRITE, &h) == ESP_OK) {
        action(h); nvs_commit(h); nvs_close(h);
    }
}

void storage_save_wifi_nvs(const char* ssid, const char* pass) {
    strncpy(wifiStaSsid, ssid, sizeof(wifiStaSsid)-1); wifiStaSsid[sizeof(wifiStaSsid)-1] = '\0';
    strncpy(wifiStaPass, pass, sizeof(wifiStaPass)-1); wifiStaPass[sizeof(wifiStaPass)-1] = '\0';
    nvs_save_generic([&](nvs_handle_t h){ nvs_set_str(h, "wifi_ssid", wifiStaSsid); nvs_set_str(h, "wifi_pass", wifiStaPass); });
    log_msg(LOG_LEVEL_INFO, "WiFi saved to NVS");
}

void storage_save_profile_path_nvs(const char* path) {
    strncpy(lastProfilePath, path, sizeof(lastProfilePath)-1);
    lastProfilePath[sizeof(lastProfilePath)-1] = '\0';
    nvs_save_generic([&](nvs_handle_t h){ nvs_set_str(h, "profile", lastProfilePath); });
    LOG_FMT(LOG_LEVEL_INFO, "Profile path saved: %s", path);
}

void storage_save_manual_settings_nvs() {
    if (!state_lock()) return;
    double ts = g_tSet; int pm = g_powerMode; int sm = g_manualSmokePwm; int fm = g_fanMode;
    state_unlock();
    nvs_save_generic([=](nvs_handle_t h){
        nvs_set_blob(h, "manual_tset", &ts, sizeof(ts));
        nvs_set_i32(h, "manual_pow",   pm);
        nvs_set_i32(h, "manual_smoke", sm);
        nvs_set_i32(h, "manual_fan",   fm);
    });
}

void storage_save_auth_nvs(const char* user, const char* pass) {
    if (!user || !pass || strlen(user) == 0 || strlen(user) >= sizeof(authUser) ||
        strlen(pass) == 0 || strlen(pass) >= sizeof(authPass)) {
        log_msg(LOG_LEVEL_ERROR, "Auth save failed: invalid length");
        return;
    }
    strncpy(authUser, user, sizeof(authUser)-1); authUser[sizeof(authUser)-1] = '\0';
    strncpy(authPass, pass, sizeof(authPass)-1); authPass[sizeof(authPass)-1] = '\0';
    nvs_save_generic([&](nvs_handle_t h){ nvs_set_str(h, "auth_user", authUser); nvs_set_str(h, "auth_pass", authPass); });
    log_msg(LOG_LEVEL_INFO, "Auth saved to NVS");
}

void storage_reset_auth_nvs() {
    authUser[0] = '\0'; authPass[0] = '\0';
    nvs_handle_t h;
    if (nvs_open("wedzarnia", NVS_READWRITE, &h) == ESP_OK) {
        nvs_erase_key(h, "auth_user"); nvs_erase_key(h, "auth_pass");
        nvs_commit(h); nvs_close(h);
    }
    LOG_FMT(LOG_LEVEL_INFO, "Auth reset to default (user=%s)", CFG_AUTH_DEFAULT_USER);
}

String storage_list_profiles_json() {
    char json[512];
    int offset = snprintf(json, sizeof(json), "[");

    File root = LittleFS.open("/profiles");
    if (!root || !root.isDirectory()) {
        log_msg(LOG_LEVEL_WARN, "Cannot open /profiles directory");
        return "[]";
    }

    bool first = true;
    File file = root.openNextFile();
    while (file) {
        if (!file.isDirectory()) {
            const char* name = file.name();
            int nameLen = strlen(name);
            if (nameLen > 5 && strcmp(name + nameLen - 5, ".prof") == 0) {
                if (!first) offset += snprintf(json+offset, sizeof(json)-offset, ",");
                offset += snprintf(json+offset, sizeof(json)-offset, "\"%s\"", name);
                first = false;
                if (offset >= (int)sizeof(json)-20) break;
            }
        }
        file = root.openNextFile();
    }
    root.close();
    snprintf(json+offset, sizeof(json)-offset, "]");
    return String(json);
}

// =====================================================================
// KLUCZOWA POPRAWKA: storage_reinit_flash()
// Poprzednia wersja: LittleFS.end() + LittleFS.begin(true) -> montowało WEWNĘTRZNY flash!
// Nowa wersja: hardware_remount_flash(false) - zawsze zewnętrzny W25Q128
// =====================================================================
bool storage_reinit_flash() {
    log_msg(LOG_LEVEL_INFO, "Re-mounting external flash (W25Q128)...");
    LittleFS.end();
    delay(200);
    bool ok = hardware_remount_flash(false);
    if (ok) log_msg(LOG_LEVEL_INFO, "Flash re-mounted OK");
    else    log_msg(LOG_LEVEL_ERROR, "Flash re-mount FAILED");
    return ok;
}

String storage_get_profile_as_json(const char* profileName) {
    char path[96];
    snprintf(path, sizeof(path), "/profiles/%s", profileName);

    if (!LittleFS.exists(path)) { LOG_FMT(LOG_LEVEL_WARN, "Profile not found: %s", path); return "[]"; }

    File f = LittleFS.open(path, "r");
    if (!f) { LOG_FMT(LOG_LEVEL_ERROR, "Cannot open: %s", path); return "[]"; }

    char json[2048];
    int offset = snprintf(json, sizeof(json), "[");
    bool first = true;
    char lineBuf[256];

    while (f.available()) {
        int len = f.readBytesUntil('\n', lineBuf, sizeof(lineBuf)-1);
        lineBuf[len] = '\0';
        char* line = lineBuf;
        while (*line == ' ' || *line == '\t') line++;
        len = strlen(line);
        while (len > 0 && (line[len-1] == '\r' || line[len-1] == '\n' || line[len-1] == ' ')) line[--len] = '\0';
        if (len == 0 || line[0] == '#') continue;

        char copy[256]; strncpy(copy, line, sizeof(copy)); copy[255] = '\0';
        char* fields[10]; int fc = 0;
        char* tok = strtok(copy, ";");
        while (tok && fc < 10) { fields[fc++] = tok; tok = strtok(NULL, ";"); }
        if (fc < 10) continue;

        if (!first) offset += snprintf(json+offset, sizeof(json)-offset, ",");
        offset += snprintf(json+offset, sizeof(json)-offset,
            "{\"name\":\"%s\",\"tSet\":%s,\"tMeat\":%s,\"minTime\":%s,"
            "\"powerMode\":%s,\"smoke\":%s,\"fanMode\":%s,"
            "\"fanOn\":%s,\"fanOff\":%s,\"useMeatTemp\":%s}",
            fields[0],fields[1],fields[2],fields[3],
            fields[4],fields[5],fields[6],fields[7],fields[8],fields[9]);
        first = false;
        if (offset >= (int)sizeof(json)-50) break;
    }
    f.close();
    snprintf(json+offset, sizeof(json)-offset, "]");
    return String(json);
}

String storage_list_github_profiles_json() {
    if (WiFi.status() != WL_CONNECTED) return "[\"Brak WiFi\"]";

    HTTPClient http; WiFiClientSecure sc; sc.setInsecure();
    http.begin(sc, CFG_GITHUB_API_URL);
    http.addHeader("User-Agent", "ESP32-Wedzarnia/3.6");
    http.setTimeout(10000);
    http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
    int code = http.GET();
    if (code != HTTP_CODE_OK) { http.end(); return "[\"Blad API GitHub\"]"; }

    String body = http.getString(); http.end();
    StaticJsonDocument<4096> doc;
    if (deserializeJson(doc, body)) return "[\"Blad parsowania\"]";

    char json[512]; int offset = snprintf(json, sizeof(json), "["); bool first = true;
    for (JsonVariant v : doc.as<JsonArray>()) {
        const char* fn = v["name"];
        if (!fn) continue;
        int nl = strlen(fn);
        if (nl > 5 && strcmp(fn+nl-5, ".prof") == 0) {
            if (!first) offset += snprintf(json+offset, sizeof(json)-offset, ",");
            offset += snprintf(json+offset, sizeof(json)-offset, "\"%s\"", fn);
            first = false;
            if (offset >= (int)sizeof(json)-50) break;
        }
    }
    snprintf(json+offset, sizeof(json)-offset, "]");
    return String(json);
}

bool storage_load_github_profile(const char* profileName) {
    if (WiFi.status() != WL_CONNECTED) {
        if (state_lock()) { g_errorProfile = true; state_unlock(); }
        return false;
    }

    HTTPClient http; WiFiClientSecure sc; sc.setInsecure();
    char url[192];
    snprintf(url, sizeof(url), "%s%s", CFG_GITHUB_PROFILES_BASE_URL, profileName);
    http.begin(sc, url);
    http.setTimeout(15000);
    http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
    http.addHeader("User-Agent", "ESP32-Wedzarnia/3.6");
    http.addHeader("Accept", "text/plain");
    int code = http.GET();
    if (code != HTTP_CODE_OK) {
        http.end();
        if (state_lock()) { g_errorProfile = true; state_unlock(); }
        return false;
    }

    String body = http.getString(); http.end();
    if (body.length() == 0) {
        if (state_lock()) { g_errorProfile = true; state_unlock(); }
        return false;
    }

    int loadedStepCount = 0, pos = 0, bodyLen = body.length();
    while (pos < bodyLen && loadedStepCount < MAX_STEPS) {
        int eol = body.indexOf('\n', pos);
        if (eol < 0) eol = bodyLen;
        char lineBuf[256];
        int ll = min((int)(eol-pos), 255);
        body.substring(pos, pos+ll).toCharArray(lineBuf, sizeof(lineBuf));
        lineBuf[ll] = '\0';
        if (parseProfileLine(lineBuf, g_profile[loadedStepCount])) loadedStepCount++;
        pos = eol + 1;
    }

    if (state_lock()) {
        g_stepCount    = loadedStepCount;
        g_errorProfile = (g_stepCount == 0);
        g_processStats.totalProcessTimeSec = 0;
        for (int i = 0; i < g_stepCount; i++)
            g_processStats.totalProcessTimeSec += g_profile[i].minTimeMs / 1000;
        state_unlock();
    }

    if (!g_errorProfile) LOG_FMT(LOG_LEVEL_INFO, "GitHub profile '%s': %d steps", profileName, g_stepCount);
    else                  LOG_FMT(LOG_LEVEL_ERROR, "GitHub profile empty: %s", profileName);
    return !g_errorProfile;
}

void storage_backup_config() {
    backupCounter++;
    if (backupCounter % 5 != 0) return;

    char backupPath[64];
    snprintf(backupPath, sizeof(backupPath), "/backup/config_%lu.bak", millis()/1000);
    File f = LittleFS.open(backupPath, "w");
    if (!f) { log_msg(LOG_LEVEL_ERROR, "Backup create failed"); return; }

    StaticJsonDocument<512> doc;
    doc["profile_path"]     = lastProfilePath;
    doc["wifi_ssid"]        = wifiStaSsid;
    doc["backup_timestamp"] = millis()/1000;
    serializeJson(doc, f); f.close();
    LOG_FMT(LOG_LEVEL_INFO, "Backup: %s", backupPath);
    cleanupOldBackups();
}

void cleanupOldBackups() {
    File dir = LittleFS.open("/backup");
    if (!dir) return;

    char files[MAX_BACKUPS+5][64]; int count = 0;
    while (File e = dir.openNextFile()) {
        if (!e.isDirectory()) {
            const char* fn = e.name(); int nl = strlen(fn);
            if (nl > 4 && strcmp(fn+nl-4, ".bak") == 0 && count < (int)(sizeof(files)/64)) {
                strncpy(files[count++], fn, 63);
            }
        }
        e.close();
    }
    dir.close();

    if (count <= MAX_BACKUPS) return;

    // Sortuj i usuń najstarsze
    for (int i = 0; i < count-1; i++)
        for (int j = 0; j < count-i-1; j++)
            if (strcmp(files[j], files[j+1]) > 0) {
                char tmp[64]; strcpy(tmp, files[j]); strcpy(files[j], files[j+1]); strcpy(files[j+1], tmp);
            }

    for (int i = 0; i < count-MAX_BACKUPS; i++) {
        char path[96]; snprintf(path, sizeof(path), "/backup/%s", files[i]);
        if (LittleFS.remove(path)) LOG_FMT(LOG_LEVEL_INFO, "Deleted backup: %s", files[i]);
    }
}

bool storage_restore_backup(const char* backupPath) {
    if (!LittleFS.exists(backupPath)) return false;
    File f = LittleFS.open(backupPath, "r");
    if (!f) return false;

    StaticJsonDocument<512> doc;
    if (deserializeJson(doc, f)) { f.close(); return false; }
    f.close();

    const char* pp = doc["profile_path"];
    const char* ws = doc["wifi_ssid"];
    if (pp) { strncpy(lastProfilePath, pp, sizeof(lastProfilePath)-1); storage_save_profile_path_nvs(lastProfilePath); }
    if (ws && strlen(ws) > 0) { strncpy(wifiStaSsid, ws, sizeof(wifiStaSsid)-1); storage_save_wifi_nvs(wifiStaSsid, wifiStaPass); }
    return true;
}

String storage_list_backups_json() {
    if (!LittleFS.exists("/backup")) return "[]";
    File dir = LittleFS.open("/backup");
    if (!dir || !dir.isDirectory()) return "[]";

    char json[512]; int offset = snprintf(json, sizeof(json), "["); bool first = true;
    while (File e = dir.openNextFile()) {
        if (!e.isDirectory()) {
            const char* fn = e.name(); int nl = strlen(fn);
            if (nl > 4 && strcmp(fn+nl-4, ".bak") == 0) {
                if (!first) offset += snprintf(json+offset, sizeof(json)-offset, ",");
                offset += snprintf(json+offset, sizeof(json)-offset, "\"%s\"", fn);
                first = false;
                if (offset >= (int)sizeof(json)-50) break;
            }
        }
        e.close();
    }
    dir.close();
    snprintf(json+offset, sizeof(json)-offset, "]");
    return String(json);
}
