// ui.cpp - Zaktualizowana wersja bez custom_font.h
#include <esp_task_wdt.h>
#include "ui.h"
#include "config.h"
#include "state.h"
#include "outputs.h"
#include "storage.h"
#include "process.h"
#include "sensors.h"
#include <climits>
#include <vector>
#include <ArduinoJson.h>
#include <WiFi.h>
#include <SD.h>

// ============================================================
// ZMIENNE STANU INTERFEJSU UZYTKOWNIKA (UI)
// ============================================================
static UiState currentUiState = UiState::UI_STATE_IDLE;
static int mainMenuIndex = 0;
static constexpr int MAIN_MENU_ITEMS = 6;
static int sourceMenuIndex = 0;
static constexpr int SOURCE_MENU_ITEMS = 2;
static std::vector<String> profileList;
static int profileMenuIndex = 0;
static bool profilesLoading = false;
static int manualEditIndex = 0;
static constexpr int MANUAL_EDIT_ITEMS = 5;
static bool editingFanOnTime = true;
static bool confirmSelection = false;
static bool force_redraw = true;
static unsigned long lastFullRedraw = 0;
static unsigned long lastUserActivity = 0;

// Nowe zmienne dla menu ustawien systemowych
static int systemSettingsIndex = 0;
static constexpr int SYSTEM_SETTINGS_ITEMS = 5;
static bool inSubMenu = false;
static int wifiSettingsIndex = 0;
static constexpr int WIFI_SETTINGS_ITEMS = 3;

// Zmienne tymczasowe dla funkcji (przeniesione na zewnatrz switch)
static bool resetConfirmed;
static unsigned long resetTimeout;
static unsigned long wifiTimeout;
static unsigned long infoTimeout;

// Struktura cache dla wyswietlacza
struct DisplayCache {
    double chamberTemp = -99.0;
    double meatTemp = -99.0;
    double setTemp = -99.0;
    String stateString = "";
    String stepName = "";
    String elapsedStr = "";
    String remainingStr = "";
    unsigned long lastUpdate = 0;
    bool needsRedraw = true;
};

static DisplayCache displayCache;

// ============================================================
// FUNKCJE POMOCNICZE DLA WYSWIETLACZA
// ============================================================

static int calculateTextWidth(const String& text, int size) {
    return text.length() * (size == 1 ? 6 : 12);
}

static void updateTextAutoSize(int16_t x, int16_t y, int16_t maxWidth, 
                              const String& oldText, const String& newText, 
                              uint16_t color) {
    if (oldText == newText && !force_redraw && !displayCache.needsRedraw) return;
    
    int textWidth = newText.length() * 12;
    uint8_t textSize = 1;
    uint8_t textHeight = 8;
    
    if (textWidth <= maxWidth && newText.length() <= 10) {
        textSize = 2;
        textHeight = 16;
    }
    
    display.setTextSize(textSize);
    display.fillRect(x, y, maxWidth, textHeight, ST77XX_BLACK);
    display.setCursor(x, y);
    display.setTextColor(color);
    display.print(newText);
}

static void updateText(int16_t x, int16_t y, int16_t w, int16_t h, 
                      const String& oldText, const String& newText, 
                      uint16_t color, uint8_t textSize) {
    if (oldText != newText || force_redraw || displayCache.needsRedraw) {
        display.setTextSize(textSize);
        display.fillRect(x, y, w, h, ST77XX_BLACK);
        display.setCursor(x, y);
        display.setTextColor(color);
        display.print(newText);
        
        // Debug log
        log_msg(LOG_LEVEL_DEBUG, 
                String("updateText: ") + oldText + " -> " + newText + 
                " size:" + textSize + " at (" + x + "," + y + ")");
    }
}

void ui_init() {
    lastUserActivity = millis();
    displayCache.lastUpdate = millis();
    systemSettingsIndex = 0;
    wifiSettingsIndex = 0;
    inSubMenu = false;
}

void ui_force_redraw() { 
    displayCache.needsRedraw = true;
    force_redraw = true; 
}

const char* getStateStringForDisplay(ProcessState st) {
    switch (st) {
        case ProcessState::IDLE:               return "Czuwanie";
        case ProcessState::RUNNING_AUTO:       return "AUTO";
        case ProcessState::RUNNING_MANUAL:     return "MANUAL";
        case ProcessState::PAUSE_DOOR:         return "Pauza: Drzwi";
        case ProcessState::PAUSE_SENSOR:       return "Pauza: Czujnik";
        case ProcessState::PAUSE_OVERHEAT:     return "Pauza: Przegrzanie";
        case ProcessState::PAUSE_HEATER_FAULT: return "AWARIA Grzalki";   // [NEW]
        case ProcessState::PAUSE_USER:         return "PAUZA";
        case ProcessState::ERROR_PROFILE:      return "Blad Profilu";
        case ProcessState::SOFT_RESUME:        return "Wznawianie...";
        default:                               return "Nieznany";
    }
}

void formatTime(char* buf, size_t len, unsigned long totalSeconds) {
    int hours = totalSeconds / 3600;
    int minutes = (totalSeconds % 3600) / 60;
    int seconds = totalSeconds % 60;
    snprintf(buf, len, "%02d:%02d:%02d", hours, minutes, seconds);
}

static void ui_transition_effect(bool forward) {
    if (!force_redraw) return;
    
    for (int i = 0; i < SCREEN_WIDTH; i += 4) {
        if (forward) {
            display.drawFastVLine(i, 0, SCREEN_HEIGHT, ST77XX_BLACK);
        } else {
            display.drawFastVLine(SCREEN_WIDTH - i, 0, SCREEN_HEIGHT, ST77XX_BLACK);
        }
        delay(1);
    }
}

static void showDiagnosticsScreen() {
    display.setTextSize(1);
    display.setCursor(0, 80);
    display.printf("Pamiec: %d B", ESP.getFreeHeap());
    display.setCursor(0, 95);
    display.printf("WiFi: %s", WiFi.status() == WL_CONNECTED ? "OK" : "OFF");
    display.setCursor(0, 110);
    display.printf("Karta SD: %s", SD.cardType() != CARD_NONE ? "OK" : "ERR");
    display.setCursor(0, 125);
    display.printf("Czas pracy: %lu s", millis() / 1000);
}

// ============================================================
// FUNKCJE OBSLUGI USTAWIEN SYSTEMOWYCH
// ============================================================


// [NEW] Wykrywanie długiego przytrzymania przycisku ENTER na ekranie IDLE
// → reset danych logowania do domyślnych z config.h
static void checkAuthResetHold() {
    // Monitoruj tylko gdy: stan IDLE i UI na ekranie głównym
    if (currentUiState != UiState::UI_STATE_IDLE) return;

    bool enterPressed = (digitalRead(PIN_BTN_ENTER) == LOW);

    static unsigned long holdStart = 0;
    static bool holdActive = false;
    static bool resetDone  = false;   // zapobiega wielokrotnemu resetowi podczas jednego przytrzymania

    if (enterPressed && !holdActive) {
        // Przycisk właśnie wciśnięty
        holdStart   = millis();
        holdActive  = true;
        resetDone   = false;
    } else if (!enterPressed) {
        // Przycisk zwolniony
        holdActive = false;
        resetDone  = false;
    } else if (holdActive && !resetDone) {
        unsigned long held = millis() - holdStart;

        // Wizualne potwierdzenie: pokaż pasek postępu po 1 s trzymania
        if (held > 1000 && held < CFG_AUTH_RESET_HOLD_MS) {
            int progress = map(held, 1000, CFG_AUTH_RESET_HOLD_MS, 0, 100);

            // Narysuj pasek postępu w dolnej części ekranu
            display.fillRect(0, 152, SCREEN_WIDTH, 8, ST77XX_BLACK);
            display.fillRect(0, 152, (SCREEN_WIDTH * progress) / 100, 8, ST77XX_RED);

            // Etykieta tylko raz przy starcie
            if (held < 1100) {
                display.setTextSize(1);
                display.setTextColor(ST77XX_RED);
                display.setCursor(15, 142);
                display.print("Reset hasla...");
            }
        }

        // Po upływie czasu – wykonaj reset
        if (held >= CFG_AUTH_RESET_HOLD_MS && !resetDone) {
            resetDone = true;

            storage_reset_auth_nvs();

            // Wyczyść obszar i pokaż komunikat
            display.fillRect(0, 130, SCREEN_WIDTH, 30, ST77XX_BLACK);
            display.setTextSize(1);
            display.setTextColor(ST77XX_GREEN);
            display.setCursor(5, 138);
            display.print("Haslo zresetowane!");
            display.setCursor(5, 150);
            display.print("Login: ");
            display.print(CFG_AUTH_DEFAULT_USER);

            buzzerBeep(3, 200, 100);

            LOG_FMT(LOG_LEVEL_INFO,
                "Auth reset via TFT hold. Default user: %s", CFG_AUTH_DEFAULT_USER);

            // Wymuś przerysowanie ekranu po 2 s
            delay(2000);
            force_redraw = true;
            displayCache.needsRedraw = true;
        }
    }
}



static void handleSystemSettingsAction() {
    display.fillScreen(ST77XX_BLACK);
    display.setTextSize(1);
    display.setTextColor(ST77XX_WHITE);
    
    char buffer[128];
    
    switch(systemSettingsIndex) {
        case 0: // WiFi
            log_msg(LOG_LEVEL_INFO, "Opening WiFi settings...");
            display.setCursor(10, 20);
            display.print("USTAWIENIA WiFi");
            display.drawFastHLine(10, 35, 108, ST77XX_WHITE);
            
            display.setCursor(10, 50);
            snprintf(buffer, sizeof(buffer), "Status: %s", 
                     WiFi.status() == WL_CONNECTED ? "Polaczono" : "Rozlaczono");
            display.print(buffer);
            
            if (WiFi.status() == WL_CONNECTED) {
                display.setCursor(10, 65);
                display.print("IP: " + WiFi.localIP().toString());
                display.setCursor(10, 80);
                display.print("SSID: " + String(storage_get_wifi_ssid()));
            }
            
            display.setCursor(10, 100);
            display.print("1. Zmien SSID/Haslo");
            display.setCursor(10, 115);
            display.print("2. Wlacz/Wylacz");
            display.setCursor(10, 130);
            display.print("3. Skanuj sieci");
            
            display.setCursor(10, 150);
            display.print("ENTER-wybierz  EXIT-powrot");
            
            currentUiState = UiState::UI_STATE_WIFI_SETTINGS;
            wifiSettingsIndex = 0;
            inSubMenu = true;
            delay(100);
            break;
            
        case 1: // Kalibracja
            log_msg(LOG_LEVEL_INFO, "Starting sensor calibration...");
            display.setCursor(10, 50);
            display.print("KALIBRACJA");
            display.drawFastHLine(10, 65, 108, ST77XX_YELLOW);
            
            display.setCursor(10, 85);
            display.print("Identyfikacja czujnikow...");
            
            // Wymus ponowne przypisanie czujnikow
            identifyAndAssignSensors();
            
            display.setCursor(10, 105);
            if (areSensorsIdentified()) {
                display.print("Kalibracja OK!");
                display.setCursor(10, 120);
                display.print("Czujnik 0: Komora");
                display.setCursor(10, 135);
                display.print("Czujnik 1: Mieso");
                buzzerBeep(3, 100, 100);
            } else {
                display.print("Blad kalibracji!");
                buzzerBeep(5, 100, 100);
            }
            
            display.setCursor(10, 150);
            display.print("EXIT - powrot");
            delay(3000);
            force_redraw = true;
            displayCache.needsRedraw = true;
            break;
            
        case 2: // Backup
            log_msg(LOG_LEVEL_INFO, "Creating system backup...");
            display.setCursor(10, 50);
            display.print("BACKUP SYSTEMU");
            display.drawFastHLine(10, 65, 108, ST77XX_GREEN);
            
            display.setCursor(10, 85);
            display.print("Tworzenie backup...");
            
            // Utworz backup konfiguracji
            storage_backup_config();
            
            display.setCursor(10, 105);
            display.print("Backup utworzony!");
            display.setCursor(10, 120);
            display.print("Plik: /backup/");
            display.setCursor(10, 135);
            display.print("Restore via web");
            
            buzzerBeep(2, 200, 100);
            delay(2500);
            force_redraw = true;
            displayCache.needsRedraw = true;
            break;
            
        case 3: // Reset statystyk
            {
                log_msg(LOG_LEVEL_INFO, "Resetting statistics...");
                display.setCursor(10, 50);
                display.print("RESET STATYSTYK");
                display.drawFastHLine(10, 65, 108, ST77XX_RED);
                
                display.setCursor(10, 85);
                display.print("Czy na pewno?");
                display.setCursor(10, 105);
                display.print("[UP/DOWN] - TAK/NIE");
                display.setCursor(10, 120);
                display.print("[ENTER] - Potwierdz");
                
                resetConfirmed = false;
                resetTimeout = millis() + 10000;
                
                while (millis() < resetTimeout) {
                    if (digitalRead(PIN_BTN_UP) == LOW) {
                        resetConfirmed = true;
                        buzzerBeep(1, 50, 0);
                        display.setCursor(10, 135);
                        display.print("WYBRANO: TAK");
                        delay(500);
                        break;
                    }
                    if (digitalRead(PIN_BTN_DOWN) == LOW) {
                        resetConfirmed = false;
                        buzzerBeep(1, 50, 0);
                        display.setCursor(10, 135);
                        display.print("WYBRANO: NIE");
                        delay(500);
                        break;
                    }
                    if (digitalRead(PIN_BTN_ENTER) == LOW && resetConfirmed) {
                        if (state_lock()) {
                            g_processStats.totalRunTime = 0;
                            g_processStats.activeHeatingTime = 0;
                            g_processStats.stepChanges = 0;
                            g_processStats.pauseCount = 0;
                            g_processStats.avgTemp = 0.0;
                            state_unlock();
                        }
                        buzzerBeep(3, 100, 100);
                        display.setCursor(10, 135);
                        display.print("STATYSTYKI ZRESETOWANE!");
                        delay(2000);
                        break;
                    }
                    if (digitalRead(PIN_BTN_EXIT) == LOW) {
                        buzzerBeep(2, 50, 50);
                        break;
                    }
                    delay(50);
                }
                
                force_redraw = true;
                displayCache.needsRedraw = true;
            }
            break;
            
        case 4: // Informacje systemowe
            {
                log_msg(LOG_LEVEL_INFO, "Displaying system info...");
                display.setCursor(10, 20);
                display.print("INFORMACJE SYSTEMOWE");
                display.drawFastHLine(10, 35, 108, ST77XX_CYAN);
                
                display.setCursor(10, 50);
                display.print("Heap: " + String(ESP.getFreeHeap()) + " B");
                display.setCursor(10, 65);
                display.print("Uptime: " + String(millis() / 1000) + "s");
                display.setCursor(10, 80);
                display.print("SD: " + String(SD.cardType() != CARD_NONE ? "OK" : "ERR"));
                display.setCursor(10, 95);
                display.print("WiFi: " + String(WiFi.status() == WL_CONNECTED ? "OK" : "OFF"));
                display.setCursor(10, 110);
                display.print("Czujniki: " + String(sensors.getDeviceCount()));
                display.setCursor(10, 125);
                display.print("Wersja: " FW_VERSION);
                display.setCursor(10, 140);
                display.print("Autor: " FW_AUTHOR);
                
                display.setCursor(10, 155);
                display.print("EXIT - powrot");
                
                // Czekaj na EXIT
                infoTimeout = millis() + 10000;
                while (millis() < infoTimeout) {
                    if (digitalRead(PIN_BTN_EXIT) == LOW) {
                        buzzerBeep(1, 50, 0);
                        break;
                    }
                    delay(50);
                }
                
                force_redraw = true;
                displayCache.needsRedraw = true;
            }
            break;
    }
}

static void handleWiFiSettingsAction() {
    display.fillScreen(ST77XX_BLACK);
    display.setTextSize(1);
    display.setTextColor(ST77XX_WHITE);
    
    switch(wifiSettingsIndex) {
        case 0: // Zmien SSID/Haslo
            display.setCursor(10, 50);
            display.print("ZMIANA WiFi");
            display.setCursor(10, 70);
            display.print("Uzyj strony web:");
            display.setCursor(10, 85);
            display.print("http://" + WiFi.softAPIP().toString());
            display.setCursor(10, 100);
            display.print("/wifi");
            display.setCursor(10, 130);
            display.print("EXIT - powrot");
            break;
            
        case 1: // Wlacz/Wlacz WiFi
            {
                display.setCursor(10, 50);
                if (WiFi.status() == WL_CONNECTED) {
                    display.print("WYLACZ WiFi?");
                    display.setCursor(10, 70);
                    display.print("[ENTER] - Wylacz");
                    display.setCursor(10, 85);
                    display.print("[EXIT] - Anuluj");
                    
                    wifiTimeout = millis() + 5000;
                    while (millis() < wifiTimeout) {
                        if (digitalRead(PIN_BTN_ENTER) == LOW) {
                            WiFi.disconnect();
                            WiFi.mode(WIFI_AP);
                            buzzerBeep(2, 100, 100);
                            display.setCursor(10, 105);
                            display.print("WiFi WYLACZONE!");
                            delay(2000);
                            break;
                        }
                        if (digitalRead(PIN_BTN_EXIT) == LOW) {
                            break;
                        }
                        delay(50);
                    }
                } else {
                    display.print("WLACZ WiFi?");
                    display.setCursor(10, 70);
                    display.print("[ENTER] - Wlacz");
                    display.setCursor(10, 85);
                    display.print("[EXIT] - Anuluj");
                    
                    wifiTimeout = millis() + 5000;
                    while (millis() < wifiTimeout) {
                        if (digitalRead(PIN_BTN_ENTER) == LOW) {
                            WiFi.begin(storage_get_wifi_ssid(), storage_get_wifi_pass());
                            buzzerBeep(2, 100, 100);
                            display.setCursor(10, 105);
                            display.print("Laczenie...");
                            delay(3000);
                            break;
                        }
                        if (digitalRead(PIN_BTN_EXIT) == LOW) {
                            break;
                        }
                        delay(50);
                    }
                }
            }
            break;
            
        case 2: // Skanuj sieci
            {
                display.setCursor(10, 50);
                display.print("SKANOWANIE SIECI");
                display.setCursor(10, 70);
                display.print("Prosze czekac...");
                
                WiFi.scanNetworks(true);
                delay(2000);
                
                int n = WiFi.scanComplete();
                display.fillRect(0, 70, 128, 90, ST77XX_BLACK);
                
                if (n > 0) {
                    display.setCursor(10, 70);
                    display.print("Znalezione: " + String(n));
                    for (int i = 0; i < min(3, n); i++) {
                        display.setCursor(10, 85 + i * 15);
                        display.print(WiFi.SSID(i).substring(0, 15));
                    }
                } else {
                    display.setCursor(10, 85);
                    display.print("Brak sieci");
                }
                
                display.setCursor(10, 130);
                display.print("EXIT - powrot");
            }
            break;
    }
    
    display.setCursor(10, 150);
    display.print("ENTER-wybierz  EXIT-powrot");
}

// ============================================================
// GLOWNA PETLA OBSLUGI PRZYCISKOW
// ============================================================
void ui_handle_buttons() {
    struct Button { 
        const uint8_t PIN; 
        bool lastState; 
        unsigned long lastPressTime; 
    };
    
    static Button buttons[] = { 
        {PIN_BTN_UP, HIGH, 0}, 
        {PIN_BTN_DOWN, HIGH, 0}, 
        {PIN_BTN_ENTER, HIGH, 0}, 
        {PIN_BTN_EXIT, HIGH, 0} 
    };
    
    const unsigned long DEBOUNCE_TIME = 200;
    unsigned long now = millis();

    for (int i = 0; i < 4; ++i) {
        bool currentState = digitalRead(buttons[i].PIN);
        if (currentState == LOW && buttons[i].lastState == HIGH && 
            (now - buttons[i].lastPressTime > DEBOUNCE_TIME)) {
            
            buttons[i].lastPressTime = now;
            buzzerBeep(1, 50, 0);
            force_redraw = true;
            displayCache.needsRedraw = true;
            int pin = buttons[i].PIN;

            state_lock();
            ProcessState proc_st = g_currentState;
            state_unlock();

            if (proc_st != ProcessState::IDLE && 
                currentUiState != UiState::UI_STATE_IDLE && 
                pin == PIN_BTN_EXIT && 
                currentUiState != UiState::UI_STATE_SYSTEM_SETTINGS &&
                currentUiState != UiState::UI_STATE_DIAGNOSTICS &&
                currentUiState != UiState::UI_STATE_WIFI_SETTINGS) {
                
                currentUiState = UiState::UI_STATE_IDLE;
                ui_transition_effect(false);
            } else {
                switch (currentUiState) {
case UiState::UI_STATE_IDLE:
    if (pin == PIN_BTN_ENTER && proc_st == ProcessState::IDLE) {
        currentUiState = UiState::UI_STATE_MENU_MAIN;
        mainMenuIndex = 0;
        ui_transition_effect(true);
    }
    if (pin == PIN_BTN_EXIT && proc_st != ProcessState::IDLE) {
        currentUiState = UiState::UI_STATE_CONFIRM_ACTION;
        mainMenuIndex = 2;
        confirmSelection = false;
        ui_transition_effect(true);
    }
    if (pin == PIN_BTN_DOWN && proc_st == ProcessState::RUNNING_AUTO) {
        currentUiState = UiState::UI_STATE_CONFIRM_NEXT_STEP;
        confirmSelection = false;
        ui_transition_effect(true);
    }
    break;
                        
                    case UiState::UI_STATE_MENU_MAIN:
                        if (pin == PIN_BTN_UP) {
                            mainMenuIndex = (mainMenuIndex - 1 + MAIN_MENU_ITEMS) % MAIN_MENU_ITEMS;
                        }
                        else if (pin == PIN_BTN_DOWN) {
                            mainMenuIndex = (mainMenuIndex + 1) % MAIN_MENU_ITEMS;
                        }
                        else if (pin == PIN_BTN_EXIT) { 
                            currentUiState = UiState::UI_STATE_IDLE;
                            ui_transition_effect(false);
                        }
                        else if (pin == PIN_BTN_ENTER) {
                            if (mainMenuIndex == 0) { 
                                currentUiState = UiState::UI_STATE_MENU_SOURCE; 
                                sourceMenuIndex = 0; 
                                ui_transition_effect(true);
                            } 
                            else if (mainMenuIndex == 1) { 
                                currentUiState = UiState::UI_STATE_EDIT_MANUAL; 
                                manualEditIndex = 0; 
                                ui_transition_effect(true);
                            } 
                            else if (mainMenuIndex == 2) { 
                                confirmSelection = false; 
                                currentUiState = UiState::UI_STATE_CONFIRM_ACTION; 
                                ui_transition_effect(true);
                            }
                            else if (mainMenuIndex == 3) { 
                                currentUiState = UiState::UI_STATE_SYSTEM_SETTINGS; 
                                systemSettingsIndex = 0;
                                ui_transition_effect(true);
                            }
                            else if (mainMenuIndex == 4) { 
                                currentUiState = UiState::UI_STATE_DIAGNOSTICS; 
                                ui_transition_effect(true);
                            }
                            else if (mainMenuIndex == 5) { 
                                // Kalibracja
                                currentUiState = UiState::UI_STATE_IDLE;
                                buzzerBeep(3, 100, 100);
                                log_msg(LOG_LEVEL_INFO, "Calibration menu selected");
                            }
                        }
                        break;
                        
                    case UiState::UI_STATE_MENU_SOURCE:
                        if (pin == PIN_BTN_UP || pin == PIN_BTN_DOWN) {
                            sourceMenuIndex = (sourceMenuIndex + 1) % SOURCE_MENU_ITEMS;
                        }
                        else if (pin == PIN_BTN_EXIT) {
                            currentUiState = UiState::UI_STATE_MENU_MAIN;
                            ui_transition_effect(false);
                        }
                        else if (pin == PIN_BTN_ENTER) { 
                            profileMenuIndex = 0; 
                            profilesLoading = true; 
                            profileList.clear(); 
                            currentUiState = UiState::UI_STATE_MENU_PROFILES; 
                            ui_transition_effect(true);
                        }
                        break;
                        
                    case UiState::UI_STATE_MENU_PROFILES:
                        { 
                            if (profilesLoading) { 
                                if (pin == PIN_BTN_EXIT) { 
                                    currentUiState = UiState::UI_STATE_MENU_SOURCE; 
                                    ui_transition_effect(false);
                                } 
                                break; 
                            }
                            
                            int listSize = profileList.size();
                            if (listSize == 0) { 
                                if (pin == PIN_BTN_EXIT) { 
                                    currentUiState = UiState::UI_STATE_MENU_SOURCE; 
                                    ui_transition_effect(false);
                                } 
                                break; 
                            }
                            
                            if (pin == PIN_BTN_UP) { 
                                profileMenuIndex = (profileMenuIndex - 1 + listSize) % listSize; 
                            }
                            else if (pin == PIN_BTN_DOWN) { 
                                profileMenuIndex = (profileMenuIndex + 1) % listSize; 
                            }
                            else if (pin == PIN_BTN_EXIT) { 
                                currentUiState = UiState::UI_STATE_MENU_SOURCE; 
                                ui_transition_effect(false);
                            }
                            else if (pin == PIN_BTN_ENTER) {
                                String selectedProfile = profileList[profileMenuIndex];
                                
                                if (sourceMenuIndex == 0) {
                                    // SD – załaduj i startuj
                                    String path = "/profiles/" + selectedProfile;
                                    storage_save_profile_path_nvs(path.c_str());
                                    if (storage_load_profile()) {
                                        process_start_auto();
                                    } else {
                                        buzzerBeep(3, 200, 100);
                                        log_msg(LOG_LEVEL_ERROR, "Failed to load SD profile");
                                    }
                                } else {
                                    // GitHub – pokaż "Pobieranie...", załaduj profil, potem startuj
                                    display.fillRect(0, 74, SCREEN_WIDTH, SCREEN_HEIGHT - 74, ST77XX_BLACK);
                                    display.setCursor(10, 95);
                                    display.setTextColor(ST77XX_YELLOW);
                                    display.print("Pobieranie...");
                                    display.setCursor(10, 108);
                                    display.print(selectedProfile.substring(0, 18));
                                    
                                    String path = "github:" + selectedProfile;
                                    storage_save_profile_path_nvs(path.c_str());
                                    
                                    // Resetuj WDT – pobieranie może trwać kilka sekund
                                    esp_task_wdt_reset();
                                    bool ok = storage_load_github_profile(selectedProfile.c_str());
                                    esp_task_wdt_reset();
                                    
                                    if (ok) {
                                        process_start_auto();
                                    } else {
                                        buzzerBeep(3, 200, 100);
                                        display.fillRect(0, 90, SCREEN_WIDTH, 30, ST77XX_BLACK);
                                        display.setCursor(10, 95);
                                        display.setTextColor(ST77XX_RED);
                                        display.print("Blad! Brak WiFi?");
                                        delay(2000);
                                        log_msg(LOG_LEVEL_ERROR, "Failed to load GitHub profile: " + selectedProfile);
                                    }
                                }
                                
                                currentUiState = UiState::UI_STATE_IDLE;
                                ui_transition_effect(false);
                            }
                        }
                        break;
                        
                    case UiState::UI_STATE_EDIT_MANUAL:
                        if (pin == PIN_BTN_EXIT) { 
                            currentUiState = UiState::UI_STATE_MENU_MAIN; 
                            ui_transition_effect(false);
                        }
                        else if (pin == PIN_BTN_ENTER) {
                            if (manualEditIndex == MANUAL_EDIT_ITEMS - 1) { 
                                process_start_manual(); 
                                currentUiState = UiState::UI_STATE_IDLE; 
                                ui_transition_effect(false);
                            }
                            else { 
                                manualEditIndex = (manualEditIndex + 1) % MANUAL_EDIT_ITEMS; 
                            }
                        } 
                        else if (pin == PIN_BTN_UP || pin == PIN_BTN_DOWN) {
                            int dir = (pin == PIN_BTN_UP) ? 1 : -1;
                            state_lock();
                            if (manualEditIndex == 0) g_tSet += dir;
                            else if (manualEditIndex == 1) g_powerMode += dir;
                            else if (manualEditIndex == 2) g_manualSmokePwm += dir * 5;
                            else if (manualEditIndex == 3) {
                                if (g_fanMode == 2) { 
                                    if (editingFanOnTime) g_fanOnTime += dir * 1000; 
                                    else g_fanOffTime += dir * 1000; 
                                }
                                else g_fanMode = (g_fanMode + dir + 3) % 3;
                            }
                            g_tSet = constrain(g_tSet, CFG_T_MIN_SET, CFG_T_MAX_SET);
                            g_powerMode = constrain(g_powerMode, CFG_POWERMODE_MIN, CFG_POWERMODE_MAX);
                            g_manualSmokePwm = constrain(g_manualSmokePwm, CFG_SMOKE_PWM_MIN, CFG_SMOKE_PWM_MAX);
                            if(g_fanOnTime < 1000) g_fanOnTime = 1000;
                            if(g_fanOffTime < 1000) g_fanOffTime = 1000;
                            state_unlock();
                        }
                        break;
                        
                    case UiState::UI_STATE_CONFIRM_ACTION:
                        if (pin == PIN_BTN_UP || pin == PIN_BTN_DOWN) { 
                            confirmSelection = !confirmSelection; 
                        }
                        else if (pin == PIN_BTN_EXIT) { 
                            currentUiState = (proc_st != ProcessState::IDLE) ? 
                                UiState::UI_STATE_IDLE : UiState::UI_STATE_MENU_MAIN; 
                            ui_transition_effect(false);
                        }
                        else if (pin == PIN_BTN_ENTER) {
                            if (confirmSelection) { 
                                allOutputsOff(); 
                                state_lock(); 
                                g_currentState = ProcessState::IDLE; 
                                state_unlock(); 
                            }
                            currentUiState = UiState::UI_STATE_IDLE;
                            ui_transition_effect(false);
                        }
                        break;
                        
                    // NOWY STAN: Potwierdzenie przejscia do nastepnego kroku
                    case UiState::UI_STATE_CONFIRM_NEXT_STEP:
                        if (pin == PIN_BTN_UP || pin == PIN_BTN_DOWN) { 
                            confirmSelection = !confirmSelection; 
                        }
                        else if (pin == PIN_BTN_EXIT) { 
                            currentUiState = UiState::UI_STATE_IDLE;
                            ui_transition_effect(false);
                        }
                        else if (pin == PIN_BTN_ENTER) {
                            if (confirmSelection) { 
                                process_force_next_step();
                            }
                            currentUiState = UiState::UI_STATE_IDLE;
                            ui_transition_effect(false);
                        }
                        break;
                        
                    case UiState::UI_STATE_SYSTEM_SETTINGS:
                        if (pin == PIN_BTN_UP) {
                            systemSettingsIndex = (systemSettingsIndex - 1 + SYSTEM_SETTINGS_ITEMS) % SYSTEM_SETTINGS_ITEMS;
                            force_redraw = true;
                            displayCache.needsRedraw = true;
                            log_msg(LOG_LEVEL_DEBUG, "System Settings UP -> index: " + String(systemSettingsIndex));
                        }
                        else if (pin == PIN_BTN_DOWN) {
                            systemSettingsIndex = (systemSettingsIndex + 1) % SYSTEM_SETTINGS_ITEMS;
                            force_redraw = true;
                            displayCache.needsRedraw = true;
                            log_msg(LOG_LEVEL_DEBUG, "System Settings DOWN -> index: " + String(systemSettingsIndex));
                        }
                        else if (pin == PIN_BTN_ENTER) {
                            log_msg(LOG_LEVEL_INFO, "System Settings ENTER -> action: " + String(systemSettingsIndex));
                            handleSystemSettingsAction();
                        }
                        else if (pin == PIN_BTN_EXIT) { 
                            currentUiState = UiState::UI_STATE_MENU_MAIN;
                            systemSettingsIndex = 0;
                            ui_transition_effect(false);
                            log_msg(LOG_LEVEL_INFO, "System Settings EXIT to main menu");
                        }
                        break;
                        
                    case UiState::UI_STATE_WIFI_SETTINGS:
                        if (pin == PIN_BTN_UP) {
                            wifiSettingsIndex = (wifiSettingsIndex - 1 + WIFI_SETTINGS_ITEMS) % WIFI_SETTINGS_ITEMS;
                            force_redraw = true;
                            displayCache.needsRedraw = true;
                        }
                        else if (pin == PIN_BTN_DOWN) {
                            wifiSettingsIndex = (wifiSettingsIndex + 1) % WIFI_SETTINGS_ITEMS;
                            force_redraw = true;
                            displayCache.needsRedraw = true;
                        }
                        else if (pin == PIN_BTN_ENTER) {
                            handleWiFiSettingsAction();
                        }
                        else if (pin == PIN_BTN_EXIT) { 
                            currentUiState = UiState::UI_STATE_SYSTEM_SETTINGS;
                            wifiSettingsIndex = 0;
                            inSubMenu = false;
                            force_redraw = true;
                            displayCache.needsRedraw = true;
                            log_msg(LOG_LEVEL_INFO, "WiFi Settings EXIT to system settings");
                        }
                        break;
                        
                    case UiState::UI_STATE_DIAGNOSTICS:
                        if (pin == PIN_BTN_EXIT) { 
                            currentUiState = UiState::UI_STATE_MENU_MAIN;
                            ui_transition_effect(false);
                        }
                        else if (pin == PIN_BTN_UP || pin == PIN_BTN_DOWN || pin == PIN_BTN_ENTER) {
                            // Pozwol na interakcje z diagnostyka
                            buzzerBeep(1, 30, 0);
                            // Mozesz dodac przewijanie informacji diagnostycznych
                        }
                        break;
                }
            }
        }
        buttons[i].lastState = currentState;
    }
}

// ============================================================
// GLOWNA FUNKCJA ODSWIEZANIA WYSWIETLACZA
// ============================================================
void ui_update_display() {
    static unsigned long lastDisplayUpdate = 0;
    static UiState lastUiState = (UiState)-1;
    static ProcessState lastProcessState = (ProcessState)-1;
    unsigned long now = millis();

    if (now - lastFullRedraw > 60000) {
        display.fillScreen(ST77XX_BLACK);
        displayCache.needsRedraw = true;
        force_redraw = true;
        lastFullRedraw = now;
    }

    if (millis() - lastDisplayUpdate < 200 && !force_redraw && !displayCache.needsRedraw) {
        return;
    }
    
    lastDisplayUpdate = millis();
    
    state_lock();
    ProcessState st = g_currentState;
    state_unlock();

    if (st != lastProcessState) {
        currentUiState = UiState::UI_STATE_IDLE;
        force_redraw = true;
        displayCache.needsRedraw = true;
    }
    lastProcessState = st;

    if (currentUiState != lastUiState) {
        force_redraw = true;
        displayCache.needsRedraw = true;
    }
    lastUiState = currentUiState;

    if (force_redraw || displayCache.needsRedraw) {
        display.fillScreen(ST77XX_BLACK);
        displayCache.chamberTemp = -99.0;
        displayCache.meatTemp = -99.0;
        displayCache.setTemp = -99.0;
        displayCache.stateString = "";
        displayCache.stepName = "";
        displayCache.elapsedStr = "";
        displayCache.remainingStr = "";
    }
    
    state_lock();
    double tc = g_tChamber;
    double tm = g_tMeat;
    double ts = g_tSet;
    int pm = g_powerMode;
    int fm = g_fanMode;
    int smoke = g_manualSmokePwm;
    unsigned long stepStartTime = g_stepStartTime;
    unsigned long processStartTime = g_processStartTime;
    int currentStep = g_currentStep;
    int stepCount = g_stepCount;
    char stepName[32];
    strncpy(stepName, (currentStep < stepCount) ? g_profile[currentStep].name : "", sizeof(stepName));
    stepName[sizeof(stepName)-1] = '\0';
    unsigned long stepTotalTimeMs = (currentStep < stepCount) ? g_profile[currentStep].minTimeMs : 0;
    state_unlock();
    
    char buf[32];
    
    // Tlo i podstawowe etykiety (tylko jesli potrzebne)
    if (force_redraw || displayCache.needsRedraw) {
        display.setTextWrap(false);
        display.setTextSize(1);
        display.setTextColor(ST77XX_WHITE);
        display.setCursor(0, 5);  
        display.print("T.kom:");
        display.setCursor(0, 27); 
        display.print("T.mie:");
        display.drawFastHLine(0, 46, SCREEN_WIDTH, ST77XX_DARKGREY);
        display.drawFastHLine(0, 72, SCREEN_WIDTH, ST77XX_DARKGREY);
    }
    
    // Temperatura komory
    updateTextAutoSize(48, 5, 80, 
                      String(displayCache.chamberTemp, 1) + " C", 
                      String(tc, 1) + " C", 
                      ST77XX_ORANGE);
    displayCache.chamberTemp = tc;
    
    // Temperatura miesa
    updateTextAutoSize(48, 27, 80, 
                      String(displayCache.meatTemp, 1) + " C", 
                      String(tm, 1) + " C", 
                      ST77XX_YELLOW);
    displayCache.meatTemp = tm;
    
    // Status i temperatura zadana
    const char* stateNameStr = getStateStringForDisplay(st);
    if (st == ProcessState::RUNNING_AUTO || st == ProcessState::RUNNING_MANUAL) {
        if(force_redraw || displayCache.needsRedraw) { 
            display.setTextSize(1); 
            display.setCursor(0, 53); 
            display.setTextColor(ST77XX_WHITE); 
            display.print("T.set:"); 
        }
        updateTextAutoSize(50, 53, 70, 
                          String(displayCache.setTemp, 1) + " C", 
                          String(ts, 1) + " C", 
                          ST77XX_CYAN);
        displayCache.setTemp = ts;
    } else {
        updateTextAutoSize(5, 53, 118, 
                          displayCache.stateString,
                          String(stateNameStr), 
                          ST77XX_CYAN);
    }
    displayCache.stateString = String(stateNameStr);
    
    // Czyszczenie dolnej czesci ekranu przy zmianie stanu UI
    if (currentUiState != lastUiState || force_redraw || displayCache.needsRedraw) {
        display.fillRect(0, 74, SCREEN_WIDTH, SCREEN_HEIGHT - 74, ST77XX_BLACK);
    }
    
    if (currentUiState == UiState::UI_STATE_IDLE) {
        if (st != ProcessState::IDLE) {
            display.setTextSize(1);
            if (st == ProcessState::RUNNING_AUTO) {
                // Nazwa kroku
                updateText(0, 80, 128, 8, 
                          displayCache.stepName, 
                          String("Krok: ") + stepName, 
                          ST77XX_WHITE, 1);
                displayCache.stepName = String("Krok: ") + stepName;

                // Czas uplyniety
                unsigned long elapsedSec = (millis() - stepStartTime) / 1000;
                formatTime(buf, sizeof(buf), elapsedSec);
                updateText(0, 95, 128, 8, 
                          displayCache.elapsedStr, 
                          String("Uplynelo: ") + buf, 
                          ST77XX_WHITE, 1);
                displayCache.elapsedStr = String("Uplynelo: ") + buf;

                // Czas pozostaly
                unsigned long totalSec = stepTotalTimeMs / 1000;
                unsigned long remainingSec = (totalSec > elapsedSec) ? totalSec - elapsedSec : 0;
                formatTime(buf, sizeof(buf), remainingSec);
                updateText(0, 110, 128, 8, 
                          displayCache.remainingStr, 
                          String("Zostalo:  ") + buf, 
                          ST77XX_WHITE, 1);
                displayCache.remainingStr = String("Zostalo:  ") + buf;
                
                // DODANE: Instrukcje bez ikon dla trybu AUTO
                display.setCursor(5, 130);
                display.print("DOWN - Nastepny krok");
                
                display.setCursor(5, 145);
                display.print("EXIT - Zatrzymaj");

} else if (st == ProcessState::RUNNING_MANUAL) {
    if(force_redraw || displayCache.needsRedraw) { 
        display.setCursor(0, 90); 
        display.print("Czas pracy:"); 
    }
    
    unsigned long elapsedSec = (millis() - processStartTime) / 1000;
    formatTime(buf, sizeof(buf), elapsedSec);

    // --- KLUCZOWA POPRAWKA ---
    // 1. USTAW poprawny rozmiar czcionki dla licznika PRZED jego aktualizacją.
    display.setTextSize(2); // Użyj rozmiaru, jaki chcesz mieć dla licznika (np. 2)

    // 2. DOPIERO TERAZ wywołaj funkcję aktualizującą
    updateTextAutoSize(10, 105, 120, 
                       displayCache.elapsedStr, 
                       buf, 
                       ST77XX_GREEN);
    displayCache.elapsedStr = buf;
    
    // 3. Ustaw rozmiar czcionki dla reszty napisów
    display.setTextSize(1);
    
    // Wyczyść obszar instrukcji (dobre praktyki z poprzedniej odpowiedzi)
    display.fillRect(0, 145, display.width(), 16, ST77XX_BLACK); 
    display.setCursor(5, 145);
    display.print("EXIT - Zatrzymaj");
}

        } else {
            // Ekran glowny (IDLE)
            display.setTextSize(2);
            display.setCursor(30, 90); 
            display.print("Menu");
            display.setCursor(25, 115);
            display.print("ENTER");
        }
    } else {
        display.setTextSize(1);
        switch (currentUiState) {
            case UiState::UI_STATE_MENU_MAIN:
                display.setCursor(0, 80); 
                display.setTextColor(mainMenuIndex == 0 ? ST77XX_GREEN : ST77XX_WHITE); 
                display.print(">Start AUTO");
                display.setCursor(0, 93); 
                display.setTextColor(mainMenuIndex == 1 ? ST77XX_GREEN : ST77XX_WHITE); 
                display.print(">Start MANUAL");
                display.setCursor(0, 106); 
                display.setTextColor(mainMenuIndex == 2 ? ST77XX_GREEN : ST77XX_WHITE); 
                display.print(">Zatrzymaj");
                display.setCursor(0, 119); 
                display.setTextColor(mainMenuIndex == 3 ? ST77XX_GREEN : ST77XX_WHITE); 
                display.print(">Ustawienia");
                display.setCursor(70, 32); 
                display.setTextColor(mainMenuIndex == 4 ? ST77XX_GREEN : ST77XX_WHITE); 
                display.print(">D");
                
                // Dodaj napisy nawigacji
                display.setCursor(10, 145);
                display.print("UP/DOWN - Wybierz");
                break;
                
            case UiState::UI_STATE_MENU_SOURCE:
                display.setCursor(10, 90); 
                display.setTextColor(sourceMenuIndex == 0 ? ST77XX_GREEN : ST77XX_WHITE); 
                display.print("Karta SD");
                display.setCursor(10, 103); 
                display.setTextColor(sourceMenuIndex == 1 ? ST77XX_GREEN : ST77XX_WHITE); 
                display.print("GitHub");
                
                display.setCursor(10, 145);
                display.print("UP/DOWN - Wybierz");
                break;
                
            case UiState::UI_STATE_MENU_PROFILES:
                if (profilesLoading) {
                    display.setCursor(10, 95);
                    display.print("Wczytywanie...");
                    
                    String json_str;
                    if (sourceMenuIndex == 0) {
                        // SD – szybkie, bez problemu ze stosem
                        json_str = storage_list_profiles_json();
                    } else {
                        // GitHub HTTPS – resetuj WDT przed i po, bo SSL może trwać kilka sekund
                        esp_task_wdt_reset();
                        json_str = storage_list_github_profiles_json();
                        esp_task_wdt_reset();
                    }
                    
                    profileList.clear();
                    DynamicJsonDocument doc(4096);  // zwiększony z 2048 – GitHub API zwraca więcej danych
                    if (deserializeJson(doc, json_str) == DeserializationError::Ok) {
                        for (JsonVariant value : doc.as<JsonArray>()) { 
                            profileList.push_back(String(value.as<const char*>())); 
                        }
                    }
                    profilesLoading = false;
                    force_redraw = true;
                    displayCache.needsRedraw = true;
                    ui_update_display(); 
                    return;
                }
                if (profileList.empty()) {
                    display.setCursor(10, 95);
                    display.print("Brak profili!");
                } else {
                    display.setTextSize(1);
                    for (size_t i = 0; i < profileList.size(); i++) {
                        if (i < 6) {
                            display.setCursor(0, 80 + i * 13);
                            if ((int)i == profileMenuIndex) { 
                                display.setTextColor(ST77XX_GREEN); 
                                display.print("> "); 
                            }
                            else { 
                                display.setTextColor(ST77XX_WHITE); 
                                display.print("  "); 
                            }
                            display.print(profileList[i]);
                        }
                    }
                }
                
                display.setCursor(10, 145);
                display.print("ENTER - Wybierz");
                break;
                
            case UiState::UI_STATE_EDIT_MANUAL:
                display.setTextSize(1);
                display.setCursor(0, 80); 
                display.setTextColor(manualEditIndex == 0 ? ST77XX_YELLOW : ST77XX_WHITE); 
                display.print("Temp: " + String(ts, 1) + " C");
                display.setCursor(0, 92); 
                display.setTextColor(manualEditIndex == 1 ? ST77XX_YELLOW : ST77XX_WHITE); 
                display.print("Moc: " + String(pm));
                display.setCursor(0, 104); 
                display.setTextColor(manualEditIndex == 2 ? ST77XX_YELLOW : ST77XX_WHITE); 
                display.print("Dym: " + String(smoke));
                display.setCursor(0, 116); 
                display.setTextColor(manualEditIndex == 3 ? ST77XX_YELLOW : ST77XX_WHITE);
                if(fm == 0) display.print("Went: OFF");
                else if (fm == 1) display.print("Went: ON");
                else display.print("Went: CYKL");
                display.setTextSize(2);
                display.setCursor(15, 135);
                display.print("START");
                
                // Dodaj legende klawiszy
                display.setTextSize(1);
                display.setCursor(0, 150);
                display.print("UP/DOWN - Zmien");
                break;
                
            case UiState::UI_STATE_CONFIRM_ACTION:
                display.setCursor(15, 95);
                display.print("Na pewno?");
                display.setTextSize(1);
                display.setCursor(10, 120); 
                display.setTextColor(!confirmSelection ? ST77XX_GREEN : ST77XX_WHITE); 
                display.print("NIE");
                display.setCursor(70, 120); 
                display.setTextColor(confirmSelection ? ST77XX_GREEN : ST77XX_WHITE); 
                display.print("TAK");
                
                display.setCursor(10, 145);
                display.print("ENTER - OK");
                break;
                
            // NOWY EKRAN: Potwierdzenie przejscia do nastepnego kroku
            case UiState::UI_STATE_CONFIRM_NEXT_STEP:
                display.setCursor(10, 85);
                display.print("Nastepny krok?");
                display.setCursor(10, 100);
                display.print("Pominac biezacy krok?");
                display.setCursor(10, 120); 
                display.setTextColor(!confirmSelection ? ST77XX_GREEN : ST77XX_WHITE); 
                display.print("NIE");
                display.setCursor(70, 120); 
                display.setTextColor(confirmSelection ? ST77XX_GREEN : ST77XX_WHITE); 
                display.print("TAK");
                
                display.setCursor(10, 145);
                display.print("ENTER - OK");
                break;
                
            case UiState::UI_STATE_SYSTEM_SETTINGS:
                {
                    display.setCursor(10, 75);
                    display.print("USTAWIENIA SYSTEMU");
                    display.drawFastHLine(10, 85, 108, ST77XX_WHITE);
                    
                    // Lista opcji z podswietleniem
                    const char* settingsItems[] = {
                        "WiFi",
                        "Kalibracja",
                        "Backup",
                        "Reset statystyk",
                        "Informacje"
                    };
                    
                    // Wyswietl 3 opcje naraz (scrollowanie)
                    int startIndex = max(0, min(systemSettingsIndex - 1, SYSTEM_SETTINGS_ITEMS - 3));
                    
                    for (int i = 0; i < min(3, SYSTEM_SETTINGS_ITEMS); i++) {
                        int itemIndex = startIndex + i;
                        int yPos = 95 + i * 15;
                        
                        if (itemIndex == systemSettingsIndex) {
                            display.setTextColor(ST77XX_YELLOW);
                            display.setCursor(5, yPos);
                            display.print("> ");
                        } else {
                            display.setTextColor(ST77XX_WHITE);
                            display.setCursor(5, yPos);
                            display.print("  ");
                        }
                        
                        display.print(settingsItems[itemIndex]);
                    }
                    
                    display.setCursor(5, 145);
                    display.print("ENTER - OK");
                }
                break;
                
            case UiState::UI_STATE_WIFI_SETTINGS:
                {
                    display.setCursor(10, 75);
                    display.print("USTAWIENIA WiFi");
                    display.drawFastHLine(10, 85, 108, ST77XX_WHITE);
                    
                    // Opcje WiFi
                    const char* wifiItems[] = {
                        "Zmien SSID/Haslo",
                        "Wlacz/Wylacz",
                        "Skanuj sieci"
                    };
                    
                    for (int i = 0; i < min(3, WIFI_SETTINGS_ITEMS); i++) {
                        int yPos = 95 + i * 15;
                        
                        if (i == wifiSettingsIndex) {
                            display.setTextColor(ST77XX_YELLOW);
                            display.setCursor(5, yPos);
                            display.print("> ");
                        } else {
                            display.setTextColor(ST77XX_WHITE);
                            display.setCursor(5, yPos);
                            display.print("  ");
                        }
                        
                        display.print(wifiItems[i]);
                    }
                    
                    display.setCursor(5, 145);
                    display.print("ENTER - Wybierz");
                }
                break;
                
            case UiState::UI_STATE_DIAGNOSTICS:
                showDiagnosticsScreen();
                display.setCursor(10, 150);
                display.print("EXIT - Powrot");
                break;
        }
    }
    
    lastUiState = currentUiState;
    force_redraw = false;
    displayCache.needsRedraw = false;
    displayCache.lastUpdate = millis();
}

void updateUserActivity() {
    lastUserActivity = millis();
}