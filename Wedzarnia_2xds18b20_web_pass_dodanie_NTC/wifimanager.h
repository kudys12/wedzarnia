// wifimanager.h - Moduł zarządzania WiFi z auto-reconnect
#pragma once
#include <Arduino.h>

// Inicjalizacja WiFi
void wifi_init();

// Utrzymywanie połączenia (auto-reconnect z exponential backoff)
void wifi_maintain_connection();

// Sprawdzanie statusu
bool wifi_is_connected();

// Statystyki WiFi
struct WiFiStats {
    unsigned long totalUptime;
    unsigned long totalDowntime;
    int disconnectCount;
    unsigned long lastDisconnect;
    unsigned long lastReconnect;
};

// Pobranie statystyk
WiFiStats wifi_get_stats();

// Reset statystyk
void wifi_reset_stats();
