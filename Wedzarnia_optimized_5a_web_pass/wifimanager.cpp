// wifimanager.cpp - [FIX] snprintf w logach
#include "wifimanager.h"
#include "config.h"
#include "storage.h"
#include "outputs.h"
#include <WiFi.h>

static unsigned long lastWiFiCheck = 0;
static unsigned long retryDelay = CFG_WIFI_RETRY_MIN_DELAY;
static bool wasConnected = false;
static WiFiStats stats = {0, 0, 0, 0, 0};
static unsigned long connectionStartTime = 0;
static unsigned long disconnectionStartTime = 0;

void wifi_init() {
    WiFi.mode(WIFI_AP_STA);

    WiFi.softAP(CFG_AP_SSID, CFG_AP_PASS);
    LOG_FMT(LOG_LEVEL_INFO, "AP Started: %s", CFG_AP_SSID);
    // [FIX] WiFi.softAPIP().toString() tworzy String ale jest jednorazowe przy starcie - OK
    char ipBuf[16];
    IPAddress apIp = WiFi.softAPIP();
    snprintf(ipBuf, sizeof(ipBuf), "%d.%d.%d.%d", apIp[0], apIp[1], apIp[2], apIp[3]);
    LOG_FMT(LOG_LEVEL_INFO, "AP IP: %s", ipBuf);

    const char* sta_ssid = storage_get_wifi_ssid();
    const char* sta_pass = storage_get_wifi_pass();

    if (sta_ssid && strlen(sta_ssid) > 0) {
        LOG_FMT(LOG_LEVEL_INFO, "Connecting to WiFi: %s", sta_ssid);
        WiFi.begin(sta_ssid, sta_pass);
        connectionStartTime = millis();

        int attempts = 0;
        while (WiFi.status() != WL_CONNECTED && attempts < 20) {
            delay(500);
            Serial.print(".");
            attempts++;
        }

        if (WiFi.status() == WL_CONNECTED) {
            log_msg(LOG_LEVEL_INFO, "WiFi connected!");
            IPAddress staIp = WiFi.localIP();
            snprintf(ipBuf, sizeof(ipBuf), "%d.%d.%d.%d", staIp[0], staIp[1], staIp[2], staIp[3]);
            LOG_FMT(LOG_LEVEL_INFO, "STA IP: %s", ipBuf);
            wasConnected = true;
            stats.lastReconnect = millis();
            buzzerBeep(2, 50, 50);
        } else {
            log_msg(LOG_LEVEL_WARN, "WiFi connection failed");
            disconnectionStartTime = millis();
        }
    } else {
        log_msg(LOG_LEVEL_INFO, "No WiFi credentials stored, running in AP-only mode");
    }
}

void wifi_maintain_connection() {
    unsigned long now = millis();

    if (now - lastWiFiCheck < CFG_WIFI_CHECK_INTERVAL) {
        return;
    }
    lastWiFiCheck = now;

    bool isConnected = (WiFi.status() == WL_CONNECTED);

    if (isConnected && !wasConnected) {
        stats.lastReconnect = now;
        if (disconnectionStartTime > 0) {
            stats.totalDowntime += (now - disconnectionStartTime);
        }
        disconnectionStartTime = 0;
        connectionStartTime = now;
        retryDelay = CFG_WIFI_RETRY_MIN_DELAY;

        char ipBuf[16];
        IPAddress ip = WiFi.localIP();
        snprintf(ipBuf, sizeof(ipBuf), "%d.%d.%d.%d", ip[0], ip[1], ip[2], ip[3]);
        LOG_FMT(LOG_LEVEL_INFO, "WiFi reconnected! IP: %s", ipBuf);
        buzzerBeep(1, 50, 0);

    } else if (!isConnected && wasConnected) {
        stats.disconnectCount++;
        stats.lastDisconnect = now;
        if (connectionStartTime > 0) {
            stats.totalUptime += (now - connectionStartTime);
        }
        connectionStartTime = 0;
        disconnectionStartTime = now;
        log_msg(LOG_LEVEL_WARN, "WiFi connection lost!");
    }

    wasConnected = isConnected;

    if (!isConnected) {
        const char* sta_ssid = storage_get_wifi_ssid();

        if (sta_ssid && strlen(sta_ssid) > 0) {
            static unsigned long lastReconnectAttempt = 0;

            if (now - lastReconnectAttempt >= retryDelay) {
                lastReconnectAttempt = now;

                LOG_FMT(LOG_LEVEL_INFO, "Attempting WiFi reconnect (delay: %lus)...",
                        retryDelay / 1000);

                WiFi.reconnect();

                retryDelay = min(retryDelay * 2, CFG_WIFI_RETRY_MAX_DELAY);
            }
        }
    }
}

bool wifi_is_connected() {
    return WiFi.status() == WL_CONNECTED;
}

WiFiStats wifi_get_stats() {
    unsigned long now = millis();
    WiFiStats currentStats = stats;

    if (wasConnected && connectionStartTime > 0) {
        currentStats.totalUptime = stats.totalUptime + (now - connectionStartTime);
    }
    if (!wasConnected && disconnectionStartTime > 0) {
        currentStats.totalDowntime = stats.totalDowntime + (now - disconnectionStartTime);
    }

    return currentStats;
}

void wifi_reset_stats() {
    stats.totalUptime = 0;
    stats.totalDowntime = 0;
    stats.disconnectCount = 0;
    stats.lastDisconnect = 0;
    stats.lastReconnect = 0;
    log_msg(LOG_LEVEL_INFO, "WiFi stats reset");
}
