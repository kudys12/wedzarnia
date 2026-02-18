// outputs.cpp - [FIX] Sprawdzanie wartości zwracanych przez lock()
#include "outputs.h"
#include "config.h"
#include "state.h"

// --- Zmienne dla brzęczyka ---
static volatile bool buzzerActive = false;
static volatile uint8_t buzzerBeepsRemaining = 0;
static volatile uint16_t buzzerOnMs = 100;
static volatile uint16_t buzzerOffMs = 100;
static volatile bool buzzerPhaseOn = false;
static volatile unsigned long buzzerPhaseEnd = 0;

// --- Zmienne dla grzałek (chronione heaterMutex) ---
static HeaterEnable he;

// --- Zmienne dla wentylatora cyklicznego (atomic) ---
static volatile bool fanState = true;
static volatile unsigned long fanTimer = 0;

void allOutputsOff() {
    // [FIX] Sprawdzenie czy udało się zablokować mutex
    if (!output_lock()) {
        log_msg(LOG_LEVEL_ERROR, "allOutputsOff: output_lock failed!");
        // Mimo to spróbuj wyłączyć wyjścia - bezpieczeństwo ważniejsze
    }
    ledcWrite(PIN_SSR1, 0);
    ledcWrite(PIN_SSR2, 0);
    ledcWrite(PIN_SSR3, 0);
    digitalWrite(PIN_FAN, LOW);
    ledcWrite(PIN_SMOKE_FAN, 0);
    output_unlock();
}

void buzzerBeep(uint8_t count, uint16_t onMs, uint16_t offMs) {
    if (buzzerActive) return;
    buzzerBeepsRemaining = count;
    buzzerOnMs = onMs;
    buzzerOffMs = offMs;
    buzzerPhaseOn = true;
    buzzerPhaseEnd = millis() + onMs;
    digitalWrite(PIN_BUZZER, HIGH);
    buzzerActive = true;
}

void handleBuzzer() {
    if (!buzzerActive) return;
    unsigned long now = millis();
    if (now >= buzzerPhaseEnd) {
        if (buzzerPhaseOn) {
            digitalWrite(PIN_BUZZER, LOW);
            buzzerBeepsRemaining--;
            if (buzzerBeepsRemaining > 0) {
                buzzerPhaseOn = false;
                buzzerPhaseEnd = now + buzzerOffMs;
            } else {
                buzzerActive = false;
            }
        } else {
            buzzerPhaseOn = true;
            buzzerPhaseEnd = now + buzzerOnMs;
            digitalWrite(PIN_BUZZER, HIGH);
        }
    }
}

void initHeaterEnable() {
    unsigned long now = millis();
    // [FIX] Sprawdzenie locka
    if (!heater_lock()) {
        log_msg(LOG_LEVEL_ERROR, "initHeaterEnable: heater_lock failed!");
        return;
    }
    he.h1 = he.h2 = he.h3 = false;
    he.t1 = now;
    he.t2 = now;
    he.t3 = now;
    heater_unlock();
}

void applySoftEnable() {
    unsigned long now = millis();
    // [FIX] Sprawdzenie locka
    if (!heater_lock()) return;
    if (now - he.t1 > 1000) he.h1 = true;
    if (now - he.t2 > 2000) he.h2 = true;
    if (now - he.t3 > 3000) he.h3 = true;
    heater_unlock();
}

bool areHeatersReady() {
    // [FIX] Sprawdzenie locka
    if (!heater_lock()) return false;
    bool ready = he.h1 && he.h2 && he.h3;
    heater_unlock();
    return ready;
}

void mapPowerToHeaters() {
    double p1 = 0, p2 = 0, p3 = 0;
    double p = constrain(pidOutput, 0, 100);

    // [FIX] Sprawdzenie locka
    if (!state_lock()) return;
    int pm = g_powerMode;
    state_unlock();

    if (pm == 1) {
        p1 = p;
    } else if (pm == 2) {
        if (p <= 50) { p1 = p * 2; }
        else { p1 = 100; p2 = (p - 50) * 2; }
    } else if (pm == 3) {
        if (p <= 33) { p1 = p * 3; }
        else if (p <= 66) { p1 = 100; p2 = (p - 33) * 3; }
        else { p1 = 100; p2 = 100; p3 = (p - 66) * 3; }
    }

    // [FIX] Sprawdzenie locka
    if (!heater_lock()) return;
    if (!he.h1) p1 = 0;
    if (!he.h2) p2 = 0;
    if (!he.h3) p3 = 0;
    heater_unlock();

    if (!output_lock()) return;
    ledcWrite(PIN_SSR1, (int)(p1 * 2.55));
    ledcWrite(PIN_SSR2, (int)(p2 * 2.55));
    ledcWrite(PIN_SSR3, (int)(p3 * 2.55));
    output_unlock();
}

void handleFanLogic() {
    // [FIX] Sprawdzenie locka
    if (!state_lock()) return;
    int fm = g_fanMode;
    unsigned long onT = g_fanOnTime;
    unsigned long offT = g_fanOffTime;
    state_unlock();

    if (fm == 0) {
        digitalWrite(PIN_FAN, LOW);

    } else if (fm == 1) {
        digitalWrite(PIN_FAN, HIGH);

    } else if (fm == 2) {
        unsigned long now = millis();

        bool currentFanState = fanState;
        unsigned long currentTimer = fanTimer;

        if (currentFanState) {
            if (now - currentTimer >= onT) {
                fanState = false;
                fanTimer = now;
                digitalWrite(PIN_FAN, LOW);
            }
        } else {
            if (now - currentTimer >= offT) {
                fanState = true;
                fanTimer = now;
                digitalWrite(PIN_FAN, HIGH);
            }
        }
    }
}
