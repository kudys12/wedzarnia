// storage.h - Zmodernizowana wersja
// [MOD] Zamiana SD na W25Q128 Flash (LittleFS)
#pragma once
#include <Arduino.h>

// Podstawowe funkcje
const char* storage_get_profile_path();
const char* storage_get_wifi_ssid();
const char* storage_get_wifi_pass();
bool storage_load_profile();
void storage_load_config_nvs();
void storage_save_wifi_nvs(const char* ssid, const char* pass);
void storage_save_profile_path_nvs(const char* path);
void storage_save_manual_settings_nvs();
String storage_list_profiles_json();
bool storage_reinit_flash();  // [MOD] Zmieniono z storage_reinit_sd()
String storage_get_profile_as_json(const char* profileName);

// Funkcje GitHub
String storage_list_github_profiles_json();
bool storage_load_github_profile(const char* profileName);

// Funkcje backup
void storage_backup_config();
bool storage_restore_backup(const char* backupPath);
String storage_list_backups_json();
void cleanupOldBackups();

// ======================================================
// [NEW] Autoryzacja – dane logowania w NVS
// ======================================================

// Zwraca aktualny login (z NVS lub domyślny z config.h)
const char* storage_get_auth_user();

// Zwraca aktualne hasło (z NVS lub domyślne z config.h)
const char* storage_get_auth_pass();

// Zapisuje nowe dane logowania do NVS
void storage_save_auth_nvs(const char* user, const char* pass);

// Przywraca domyślne dane logowania (kasuje wpisy z NVS)
void storage_reset_auth_nvs();
