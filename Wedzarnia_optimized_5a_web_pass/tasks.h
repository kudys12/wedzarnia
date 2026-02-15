// tasks.h - Zmodernizowana wersja
#pragma once
#include <Arduino.h>

// Tworzenie wszystkich zadań
void tasks_create_all();

// Status watchdog
String getTaskWatchdogStatus();