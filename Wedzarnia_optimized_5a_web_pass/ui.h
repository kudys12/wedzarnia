#pragma once

// Definicja stanow UI
enum class UiState {
    UI_STATE_IDLE,          // Ekran glowny (dashboard)
    UI_STATE_MENU_MAIN,     // Glowne menu nawigacyjne
    UI_STATE_MENU_SOURCE,   // Menu wyboru zrodla (SD/GitHub)
    UI_STATE_MENU_PROFILES, // Menu z lista profili
    UI_STATE_EDIT_MANUAL,   // Ekran edycji trybu manualnego
    UI_STATE_CONFIRM_ACTION, // Ekran potwierdzenia (Tak/Nie)
    UI_STATE_CONFIRM_NEXT_STEP, // NOWE: Potwierdzenie przejscia do nastepnego kroku
    UI_STATE_SYSTEM_SETTINGS, // Ustawienia systemowe
    UI_STATE_WIFI_SETTINGS,   // Ustawienia WiFi
    UI_STATE_DIAGNOSTICS    // Ekran diagnostyki
};

// Glowna funkcja inicjalizujaca UI
void ui_init();

// Aktualizacja wyswietlacza
void ui_update_display();

// Obsluga przyciskow
void ui_handle_buttons();

// Wymuszenie przerysowania ekranu
void ui_force_redraw();