// process.cpp - [FIX] Ochrona g_currentStep mutexem, eliminacja race conditions
// [NEW]  Zabezpieczenie: grzałka ON bez wzrostu temperatury → PAUSE_HEATER_FAULT
#include "process.h"
#include "config.h"
#include "state.h"
#include "outputs.h"
#include "ui.h"

// Struktura dla adaptacyjnego PID
struct AdaptivePID {
    double errorHistory[10] = {0};
    int historyIndex = 0;
    unsigned long lastAdaptation = 0;
    double currentKp = CFG_Kp;
    double currentKi = CFG_Ki;
    double currentKd = CFG_Kd;
};

static AdaptivePID adaptivePid;

// Historia temperatury dla predykcyjnego sterowania wentylatorem
static double tempHistory[5] = {0};
static int tempHistoryIndex = 0;

// ======================================================
// [NEW] MONITOR AWARII GRZAŁKI
// ======================================================

struct HeaterFaultMonitor {
    double  tempAtWindowStart = 0.0;   // temperatura komory na początku okna pomiarowego
    unsigned long windowStart = 0;     // millis() kiedy zaczęło się okno
    bool    monitoring = false;        // czy okno jest aktywne
};

static HeaterFaultMonitor hfm;

// Resetuje stan monitora – wywołuj przy każdym starcie i wznowieniu procesu
void resetHeaterFaultMonitor() {
    hfm.tempAtWindowStart = 0.0;
    hfm.windowStart = 0;
    hfm.monitoring = false;
    log_msg(LOG_LEVEL_INFO, "Heater fault monitor reset");
}

/**
 * checkHeaterEfficiency()
 *
 * Sprawdza, czy grzałka faktycznie podgrzewa komorę.
 * Monitoring jest aktywny TYLKO gdy spełnione są WSZYSTKIE trzy warunki:
 *   1. Proces jest w stanie RUNNING_AUTO lub RUNNING_MANUAL
 *   2. Temperatura komory jest co najmniej HEATER_FAULT_MIN_ERROR (10°C) poniżej setpointu
 *      → wyklucza fazę utrzymania temperatury, gdzie PID celowo redukuje moc
 *   3. Wyjście PID przekracza HEATER_FAULT_MIN_PID (50%)
 *      → grzałka faktycznie powinna grzać, a nie tylko delikatnie dogrzewać
 *
 * Okno pomiarowe trwa HEATER_NO_RISE_TIMEOUT_MS (20 minut).
 * Jeśli w tym czasie temperatura nie wzrosła o HEATER_MIN_TEMP_RISE (2°C) → AWARIA.
 * Jeśli wzrosła – okno przesuwa się do przodu (nowy punkt startowy = aktualna temp).
 * Gdy któryś z warunków odpada (np. temp doszła do celu) → monitoring wyłączany, reset.
 */
static void checkHeaterEfficiency() {
    if (!state_lock()) return;
    double currentTemp  = g_tChamber;
    double setpoint     = g_tSet;
    double pid          = pidOutput;
    ProcessState st     = g_currentState;
    state_unlock();

    bool isRunning = (st == ProcessState::RUNNING_AUTO ||
                      st == ProcessState::RUNNING_MANUAL);

    // Wszystkie trzy warunki muszą być spełnione jednocześnie
    bool shouldBeHeating = isRunning
                        && (setpoint - currentTemp) > HEATER_FAULT_MIN_ERROR
                        && pid > HEATER_FAULT_MIN_PID;

    if (shouldBeHeating && !hfm.monitoring) {
        // --- START nowego okna pomiarowego ---
        hfm.tempAtWindowStart = currentTemp;
        hfm.windowStart       = millis();
        hfm.monitoring        = true;
        LOG_FMT(LOG_LEVEL_DEBUG,
                "HeaterFault: monitoring started (T=%.1f, set=%.1f, PID=%.0f%%)",
                currentTemp, setpoint, pid);

    } else if (!shouldBeHeating && hfm.monitoring) {
        // --- Warunki przestały być spełnione – reset bez alarmu ---
        // Normalne sytuacje: temp doszła blisko celu, PID zredukował moc,
        // drzwi, pauza, itp.
        hfm.monitoring = false;
        LOG_FMT(LOG_LEVEL_DEBUG,
                "HeaterFault: monitoring stopped (T=%.1f, set=%.1f, PID=%.0f%%)",
                currentTemp, setpoint, pid);

    } else if (shouldBeHeating && hfm.monitoring) {
        // --- Okno pomiarowe trwa – sprawdź po upływie czasu ---
        unsigned long elapsed = millis() - hfm.windowStart;

        if (elapsed >= HEATER_NO_RISE_TIMEOUT_MS) {
            double rise = currentTemp - hfm.tempAtWindowStart;

            if (rise < HEATER_MIN_TEMP_RISE) {
                // ========================================
                // AWARIA POTWIERDZONA
                // ========================================
                if (state_lock()) {
                    g_currentState = ProcessState::PAUSE_HEATER_FAULT;
                    g_processStats.pauseCount++;
                    state_unlock();
                }
                allOutputsOff();
                // 5 sygnałów: wyraźnie różny od innych alarmów (2-3 sygnały)
                buzzerBeep(5, 300, 200);

                LOG_FMT(LOG_LEVEL_ERROR,
                        "!!! HEATER FAULT !!! No temp rise in %lu min",
                        HEATER_NO_RISE_TIMEOUT_MS / 60000UL);
                LOG_FMT(LOG_LEVEL_ERROR,
                        "  T at window start: %.1f C", hfm.tempAtWindowStart);
                LOG_FMT(LOG_LEVEL_ERROR,
                        "  T now:             %.1f C", currentTemp);
                LOG_FMT(LOG_LEVEL_ERROR,
                        "  Rise:              %.1f C (min required: %.1f C)",
                        rise, HEATER_MIN_TEMP_RISE);
                LOG_FMT(LOG_LEVEL_ERROR,
                        "  Setpoint:          %.1f C, PID output: %.0f%%",
                        setpoint, pid);

                hfm.monitoring = false;

            } else {
                // Temperatura rośnie prawidłowo – przesuń okno do przodu
                LOG_FMT(LOG_LEVEL_DEBUG,
                        "HeaterFault: window OK (rise=%.1f C), advancing window",
                        rise);
                hfm.tempAtWindowStart = currentTemp;
                hfm.windowStart       = millis();
            }
        }
    }
}

// ======================================================
// STATYSTYKI I ADAPTACJA PID
// ======================================================

static void updateProcessStats() {
    if (!state_lock()) return;

    unsigned long now = millis();
    unsigned long elapsed = now - g_processStats.lastUpdate;

    if (g_currentState == ProcessState::RUNNING_AUTO ||
        g_currentState == ProcessState::RUNNING_MANUAL) {
        g_processStats.totalRunTime += elapsed;

        if (pidOutput > 5.0) {
            g_processStats.activeHeatingTime += elapsed;
        }

        if (g_processStats.avgTemp == 0.0) {
            g_processStats.avgTemp = g_tChamber;
        } else {
            constexpr double alpha = 0.1;
            g_processStats.avgTemp = alpha * g_tChamber + (1.0 - alpha) * g_processStats.avgTemp;
        }

        if (g_currentState == ProcessState::RUNNING_AUTO) {
            unsigned long elapsedTotal = (now - g_processStartTime) / 1000;

            unsigned long completedTime = 0;
            for (int i = 0; i < g_currentStep; i++) {
                if (i < g_stepCount) {
                    completedTime += g_profile[i].minTimeMs / 1000;
                }
            }

            unsigned long stepElapsed = (now - g_stepStartTime) / 1000;
            unsigned long stepTotal = 0;
            if (g_currentStep >= 0 && g_currentStep < g_stepCount) {
                stepTotal = g_profile[g_currentStep].minTimeMs / 1000;
            }
            unsigned long stepRemaining = (stepTotal > stepElapsed) ? (stepTotal - stepElapsed) : 0;

            unsigned long futureTime = 0;
            for (int i = g_currentStep + 1; i < g_stepCount; i++) {
                futureTime += g_profile[i].minTimeMs / 1000;
            }

            g_processStats.remainingProcessTimeSec = stepRemaining + futureTime;
        } else {
            g_processStats.remainingProcessTimeSec = 0;
        }
    }

    g_processStats.lastUpdate = now;
    state_unlock();
}

static void adaptPidParameters() {
    unsigned long now = millis();
    if (now - adaptivePid.lastAdaptation < PID_ADAPTATION_INTERVAL) return;

    double currentError = pidSetpoint - pidInput;

    adaptivePid.errorHistory[adaptivePid.historyIndex] = currentError;
    adaptivePid.historyIndex = (adaptivePid.historyIndex + 1) % 10;

    double errorMean = 0;
    double errorVariance = 0;
    int validCount = 0;

    for (int i = 0; i < 10; i++) {
        if (fabs(adaptivePid.errorHistory[i]) < 50) {
            errorMean += adaptivePid.errorHistory[i];
            validCount++;
        }
    }

    if (validCount > 0) {
        errorMean /= validCount;

        for (int i = 0; i < 10; i++) {
            if (fabs(adaptivePid.errorHistory[i]) < 50) {
                errorVariance += pow(adaptivePid.errorHistory[i] - errorMean, 2);
            }
        }
        errorVariance /= validCount;

        if (errorVariance > 5.0) {
            adaptivePid.currentKp = CFG_Kp * 0.8;
            adaptivePid.currentKi = CFG_Ki * 0.5;
            adaptivePid.currentKd = CFG_Kd * 1.2;
        } else if (errorVariance < 0.5 && fabs(currentError) < 2.0) {
            adaptivePid.currentKp = CFG_Kp * 1.2;
            adaptivePid.currentKi = CFG_Ki * 0.8;
            adaptivePid.currentKd = CFG_Kd * 0.8;
        } else {
            adaptivePid.currentKp = CFG_Kp;
            adaptivePid.currentKi = CFG_Ki;
            adaptivePid.currentKd = CFG_Kd;
        }

        pid.SetTunings(adaptivePid.currentKp, adaptivePid.currentKi, adaptivePid.currentKd);

        if (adaptivePid.lastAdaptation > 0) {
            LOG_FMT(LOG_LEVEL_DEBUG, "PID adapted: Kp=%.2f Ki=%.2f Kd=%.2f var=%.2f",
                     adaptivePid.currentKp, adaptivePid.currentKi, adaptivePid.currentKd,
                     errorVariance);
        }
    }

    adaptivePid.lastAdaptation = now;
}

// ======================================================
// PREDYKCYJNE STEROWANIE WENTYLATOREM
// ======================================================

void predictiveFanControl() {
    double currentTemp = 0;
    if (state_lock()) {
        currentTemp = g_tChamber;
        state_unlock();
    }

    tempHistory[tempHistoryIndex] = currentTemp;
    tempHistoryIndex = (tempHistoryIndex + 1) % 5;

    double trend = 0;
    int validSamples = 0;
    for (int i = 1; i < 5; i++) {
        int idx = (tempHistoryIndex - i + 5) % 5;
        int prevIdx = (idx - 1 + 5) % 5;

        if (tempHistory[idx] > 0 && tempHistory[prevIdx] > 0) {
            trend += (tempHistory[idx] - tempHistory[prevIdx]);
            validSamples++;
        }
    }

    if (validSamples > 0) {
        trend /= validSamples;

        if (state_lock()) {
            if (g_fanMode == 2) {
                if (trend > 0.5) {
                    g_fanOnTime = min((unsigned long)(g_fanOnTime * 1.5), 30000UL);
                    g_fanOffTime = max((unsigned long)(g_fanOffTime * 0.7), 10000UL);
                } else if (trend < -0.2) {
                    g_fanOnTime = max((unsigned long)(g_fanOnTime * 0.7), 5000UL);
                    g_fanOffTime = min((unsigned long)(g_fanOffTime * 1.3), 120000UL);
                } else if (fabs(trend) < 0.1 && fabs(g_tChamber - g_tSet) < 3.0) {
                    g_fanOnTime = 10000UL;
                    g_fanOffTime = 60000UL;
                }
            }
            state_unlock();
        }
    }
}

// ======================================================
// TRYB AUTO
// ======================================================

static void handleAutoMode() {
    if (!state_lock()) return;
    int step = g_currentStep;
    int count = g_stepCount;
    unsigned long stepStart = g_stepStartTime;
    double meat = g_tMeat;
    state_unlock();

    if (step < 0 || step >= count) {
        LOG_FMT(LOG_LEVEL_ERROR, "Invalid step in AUTO mode: %d", step);
        return;
    }

    // [FIX] Kopiujemy dane kroku pod lockiem, operujemy na kopii
    Step localStep;
    if (!state_lock()) return;
    memcpy(&localStep, &g_profile[step], sizeof(Step));
    state_unlock();

    unsigned long elapsed = millis() - stepStart;

    bool timeOk = (elapsed >= localStep.minTimeMs);
    bool meatOk = (!localStep.useMeatTemp) || (meat >= localStep.tMeatTarget);

    if (timeOk && meatOk) {
        // [FIX] g_currentStep++ chroniony mutexem
        if (!state_lock()) return;
        g_currentStep++;
        int newStep = g_currentStep;
        int totalSteps = g_stepCount;
        g_processStats.stepChanges++;
        state_unlock();

        if (newStep >= totalSteps) {
            if (state_lock()) {
                g_currentState = ProcessState::PAUSE_USER;
                state_unlock();
            }
            allOutputsOff();
            buzzerBeep(3, 200, 200);
            log_msg(LOG_LEVEL_INFO, "Profile completed!");
        } else {
            applyCurrentStep();
            // Reset monitora awarii grzałki przy zmianie kroku –
            // nowy krok może mieć inną temp. startową
            resetHeaterFaultMonitor();
            buzzerBeep(2, 100, 100);
            LOG_FMT(LOG_LEVEL_INFO, "Advanced to step %d", newStep);
        }
    }

    predictiveFanControl();
    handleFanLogic();

    // [FIX] Odczyt step pod lockiem do sterowania smoke
    if (!state_lock()) return;
    step = g_currentStep;
    count = g_stepCount;
    int smokePwm = 0;
    if (step >= 0 && step < count) {
        smokePwm = g_profile[step].smokePwm;
    }
    state_unlock();

    if (step >= 0 && step < count) {
        if (output_lock()) {
            ledcWrite(PIN_SMOKE_FAN, smokePwm);
            output_unlock();
        }
    }
}

// ======================================================
// TRYB MANUALNY
// ======================================================

static void handleManualMode() {
    predictiveFanControl();
    handleFanLogic();

    if (!state_lock()) return;
    int smoke = g_manualSmokePwm;
    state_unlock();

    if (output_lock()) {
        ledcWrite(PIN_SMOKE_FAN, smoke);
        output_unlock();
    }
}

// ======================================================
// ZASTOSOWANIE KROKU PROFILU
// ======================================================

void applyCurrentStep() {
    if (!state_lock()) return;
    int step = g_currentStep;
    int count = g_stepCount;
    state_unlock();

    if (step < 0 || step >= count) {
        LOG_FMT(LOG_LEVEL_ERROR, "Cannot apply step - invalid index: %d", step);
        return;
    }

    if (state_lock()) {
        Step& s = g_profile[step];
        g_tSet = s.tSet;
        g_powerMode = s.powerMode;
        g_manualSmokePwm = s.smokePwm;
        g_fanMode = s.fanMode;
        g_fanOnTime = s.fanOnTime;
        g_fanOffTime = s.fanOffTime;
        // [FIX] g_stepStartTime ustawiane wewnątrz locka
        g_stepStartTime = millis();
        state_unlock();
    }

    LOG_FMT(LOG_LEVEL_INFO, "Step %d applied", step);
    ui_force_redraw();
}

// ======================================================
// STARTY I WZNOWIENIE PROCESU
// ======================================================

void process_start_auto() {
    // [FIX] g_currentStep ustawiane pod lockiem
    if (state_lock()) {
        g_currentStep = 0;
        state_unlock();
    }
    applyCurrentStep();
    initHeaterEnable();

    if (state_lock()) {
        g_processStartTime = millis();
        g_currentState = ProcessState::RUNNING_AUTO;
        g_lastRunMode = RunMode::MODE_AUTO;
        g_processStats.totalRunTime = 0;
        g_processStats.activeHeatingTime = 0;
        g_processStats.stepChanges = 0;
        g_processStats.pauseCount = 0;
        g_processStats.avgTemp = 0.0;
        g_processStats.lastUpdate = millis();
        adaptivePid.currentKp = CFG_Kp;
        adaptivePid.currentKi = CFG_Ki;
        adaptivePid.currentKd = CFG_Kd;
        pid.SetTunings(CFG_Kp, CFG_Ki, CFG_Kd);
        state_unlock();
    }

    // [NEW] Reset monitora awarii grzałki przy starcie
    resetHeaterFaultMonitor();

    log_msg(LOG_LEVEL_INFO, "AUTO mode started");
}

void process_start_manual() {
    if (state_lock()) {
        g_tSet = 70;
        g_powerMode = 2;
        g_manualSmokePwm = 0;
        g_fanMode = 1;
        state_unlock();
    }
    initHeaterEnable();

    if (state_lock()) {
        g_processStartTime = millis();
        g_currentState = ProcessState::RUNNING_MANUAL;
        g_lastRunMode = RunMode::MODE_MANUAL;
        g_processStats.totalRunTime = 0;
        g_processStats.activeHeatingTime = 0;
        g_processStats.stepChanges = 0;
        g_processStats.pauseCount = 0;
        g_processStats.avgTemp = 0.0;
        g_processStats.lastUpdate = millis();
        state_unlock();
    }

    // [NEW] Reset monitora awarii grzałki przy starcie
    resetHeaterFaultMonitor();

    log_msg(LOG_LEVEL_INFO, "MANUAL mode started");
}

void process_resume() {
    initHeaterEnable();
    if (state_lock()) {
        g_currentState = ProcessState::SOFT_RESUME;
        state_unlock();
    }
    // [NEW] Reset monitora awarii grzałki przy wznowieniu –
    // po pauzie temperatura może być inna niż przed pauzą
    resetHeaterFaultMonitor();

    log_msg(LOG_LEVEL_INFO, "Process resuming...");
}

// ======================================================
// GŁÓWNA LOGIKA STEROWANIA (wywoływana co 100 ms z taskControl)
// ======================================================

void process_run_control_logic() {
    extern double pidInput, pidSetpoint;

    if (!state_lock()) return;
    ProcessState st = g_currentState;
    pidInput = g_tChamber;
    pidSetpoint = g_tSet;
    unsigned long processStart = g_processStartTime;
    state_unlock();

    // Sprawdzenie maksymalnego czasu procesu
    if ((st == ProcessState::RUNNING_AUTO || st == ProcessState::RUNNING_MANUAL) &&
        (millis() - processStart > CFG_MAX_PROCESS_TIME_MS)) {
        if (state_lock()) {
            g_currentState = ProcessState::PAUSE_USER;
            state_unlock();
        }
        allOutputsOff();
        buzzerBeep(4, 150, 150);
        log_msg(LOG_LEVEL_WARN, "Max process time reached!");
        return;
    }

    switch (st) {
        case ProcessState::RUNNING_AUTO:
            adaptPidParameters();
            pid.Compute();
            applySoftEnable();
            mapPowerToHeaters();
            handleAutoMode();
            updateProcessStats();
            checkHeaterEfficiency();   // [NEW]
            break;

        case ProcessState::RUNNING_MANUAL:
            pid.Compute();
            applySoftEnable();
            mapPowerToHeaters();
            handleManualMode();
            updateProcessStats();
            checkHeaterEfficiency();   // [NEW]
            break;

        case ProcessState::SOFT_RESUME:
            pid.Compute();
            applySoftEnable();
            mapPowerToHeaters();

            if (areHeatersReady()) {
                if (state_lock()) {
                    g_currentState = (g_lastRunMode == RunMode::MODE_AUTO)
                        ? ProcessState::RUNNING_AUTO
                        : ProcessState::RUNNING_MANUAL;
                    state_unlock();
                }
                log_msg(LOG_LEVEL_INFO, "Process resumed from pause");
            }
            break;

        case ProcessState::IDLE:
        case ProcessState::PAUSE_DOOR:
        case ProcessState::PAUSE_SENSOR:
        case ProcessState::PAUSE_OVERHEAT:
        case ProcessState::PAUSE_USER:
        case ProcessState::PAUSE_HEATER_FAULT:   // [NEW]
        case ProcessState::ERROR_PROFILE:
            allOutputsOff();
            break;
    }
}

// ======================================================
// FUNKCJE POMOCNICZE
// ======================================================

void process_force_next_step() {
    if (!state_lock()) return;

    if (g_currentState != ProcessState::RUNNING_AUTO) {
        log_msg(LOG_LEVEL_WARN, "Cannot skip step - not in AUTO mode");
        state_unlock();
        return;
    }

    int nextStep = g_currentStep + 1;
    if (nextStep >= g_stepCount) {
        log_msg(LOG_LEVEL_WARN, "Cannot skip step - already at last step");
        state_unlock();
        return;
    }

    // [FIX] g_currentStep ustawiane wewnątrz locka
    g_currentStep = nextStep;
    state_unlock();

    applyCurrentStep();
    // [NEW] Reset monitora przy ręcznym przejściu do następnego kroku
    resetHeaterFaultMonitor();

    LOG_FMT(LOG_LEVEL_INFO, "Step skipped to %d", nextStep);
    buzzerBeep(1, 100, 0);
}

String getPidParameters() {
    char buffer[128];
    snprintf(buffer, sizeof(buffer),
             "Kp=%.2f, Ki=%.2f, Kd=%.2f (base: %.1f,%.1f,%.1f)",
             adaptivePid.currentKp, adaptivePid.currentKi, adaptivePid.currentKd,
             CFG_Kp, CFG_Ki, CFG_Kd);
    return String(buffer);
}

void resetAdaptivePid() {
    adaptivePid.currentKp = CFG_Kp;
    adaptivePid.currentKi = CFG_Ki;
    adaptivePid.currentKd = CFG_Kd;
    pid.SetTunings(CFG_Kp, CFG_Ki, CFG_Kd);

    for (int i = 0; i < 10; i++) {
        adaptivePid.errorHistory[i] = 0;
    }
    adaptivePid.historyIndex = 0;
    adaptivePid.lastAdaptation = 0;

    log_msg(LOG_LEVEL_INFO, "Adaptive PID reset to defaults");
}
