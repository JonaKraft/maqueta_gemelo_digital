/**
 * @file PDIData.cpp
 * @brief Implementación de utilidades de conversión para estructuras PDI
 */

#include "PDIData.h"

const char* machineStateToStr(MachineState state) {
    switch (state) {
        case MachineState::EXECUTING: return "Ejecutando";
        case MachineState::STOPPED:   return "Detenido";
        case MachineState::IDLE:      return "Idle";
        case MachineState::HELD:      return "Held";
        case MachineState::ABORTED:   return "Aborted";
        default:                      return "Desconocido";
    }
}

const char* unitModeToStr(UnitMode mode) {
    switch (mode) {
        case UnitMode::AUTO:   return "Auto";
        case UnitMode::MANUAL: return "Manual";
        case UnitMode::SETUP:  return "Setup";
        default:               return "Desconocido";
    }
}

const char* qualitySignalToStr(QualitySignal q) {
    switch (q) {
        case QualitySignal::OK:        return "OK";
        case QualitySignal::BAD:       return "Bad";
        case QualitySignal::UNCERTAIN: return "Uncertain";
        default:                       return "Desconocido";
    }
}

const char* deviceModeToStr(DeviceMode mode) {
    switch (mode) {
        case DeviceMode::OFF:       return "OFF";
        case DeviceMode::ONLINE:    return "ONLINE";
        case DeviceMode::SIMULATOR: return "SIMULADOR";
        default:                    return "Desconocido";
    }
}

MachineState strToMachineState(const char* str) {
    if (strcmp(str, "Ejecutando") == 0 || strcmp(str, "EXECUTING") == 0)
        return MachineState::EXECUTING;
    if (strcmp(str, "Detenido") == 0 || strcmp(str, "STOPPED") == 0)
        return MachineState::STOPPED;
    if (strcmp(str, "Idle") == 0 || strcmp(str, "IDLE") == 0)
        return MachineState::IDLE;
    if (strcmp(str, "Held") == 0 || strcmp(str, "HELD") == 0)
        return MachineState::HELD;
    if (strcmp(str, "Aborted") == 0 || strcmp(str, "ABORTED") == 0)
        return MachineState::ABORTED;
    return MachineState::IDLE;
}
