// outputs.h
#pragma once
#include <cstdint>

void allOutputsOff();
void buzzerBeep(uint8_t count, uint16_t onMs = 100, uint16_t offMs = 100);
void handleBuzzer();
void initHeaterEnable();
void applySoftEnable();
void mapPowerToHeaters();
void handleFanLogic();
bool areHeatersReady();  // NOWE: sprawdza czy wszystkie grzałki soft-enabled
