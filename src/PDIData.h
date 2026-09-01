/**
 * @file PDIData.h
 * @brief Estructuras de datos centrales del PDI (Plant Data Interface)
 *
 * Define todas las variables PackML/ISA-88 que el dispositivo maneja,
 * tanto las obligatorias como las extendidas (stubs).
 * Esta estructura es compartida entre MQTT, Display, Simulador y LED.
 */

#ifndef PDI_DATA_H
#define PDI_DATA_H

#include <Arduino.h>

// ─────────────────────────────────────────────────────────────────────────────
// Enumeraciones de estado PackML
// ─────────────────────────────────────────────────────────────────────────────

/** Estados operativos de la máquina (colores asociados al LED RGB) */
enum class MachineState : uint8_t {
    EXECUTING = 0,   // Verde
    STOPPED   = 1,   // Rojo
    IDLE      = 2,   // Amarillo
    HELD      = 3,   // Azul (extensión futura)
    ABORTED   = 4    // Rojo parpadeante (extensión futura)
};

/** Modos de operación de la unidad */
enum class UnitMode : uint8_t {
    AUTO   = 0,
    MANUAL = 1,
    SETUP  = 2
};

/** Señal de calidad */
enum class QualitySignal : uint8_t {
    OK        = 0,
    BAD       = 1,
    UNCERTAIN = 2
};

/** Modos del dispositivo (selector de 3 estados) */
enum class DeviceMode : uint8_t {
    OFF       = 0,   // Desconectado de red, modo offline puro
    ONLINE    = 1,   // Funcionamiento normal con Broker
    SIMULATOR = 2    // Datos simulados para demo
};

// ─────────────────────────────────────────────────────────────────────────────
// Estructura base del PDI
// ─────────────────────────────────────────────────────────────────────────────

struct PDI_Status {
    MachineState stateCurrent;
    MachineState statePrevious;       // Extendida
    uint32_t     stateDuration;       // Extendida: segundos en el estado actual
    UnitMode     unitModeCurrent;
    float        machSpeed;           // Velocidad nominal
    float        curMachSpeed;        // Velocidad actual
};

struct PDI_Admin {
    uint32_t prodProcessedCount;      // Piezas procesadas totales
    uint32_t prodDefectiveCount;      // Piezas defectuosas
};

/** Contexto de producción (extendido - stubs) */
struct PDI_Context {
    String orderNo;
    String productCode;
    String recipeId;
    String lotBatch;
    String lineId;
};

/** Alarmas y paradas (extendido - stubs) */
struct PDI_Alarms {
    String   stopReasonCode;
    String   stopReasonDesc;
    bool     alarmActive;
    String   alarmCode;
    String   alarmText;
};

/** Consumos y mediciones (extendido - stubs) */
struct PDI_Measurements {
    float power;           // Potencia en W
    float current;         // Corriente en A
    float airPressure;     // Presión de aire en bar
};

// ─────────────────────────────────────────────────────────────────────────────
// Estructura principal del Gemelo Digital
// ─────────────────────────────────────────────────────────────────────────────

struct PDIData {
    // --- Identificación ---
    String   equipmentName;
    String   assetId;
    uint32_t timestamp;              // Epoch UNIX

    // --- Estado operativo ---
    PDI_Status status;

    // --- Administración de producción ---
    PDI_Admin admin;

    // --- Valores calculados ---
    uint32_t goodCount() const {
        return admin.prodProcessedCount - admin.prodDefectiveCount;
    }

    // --- Calidad ---
    QualitySignal qualitySignal;

    // --- Conectividad ---
    bool connectionStatus;

    // --- Extendidas (stubs preparados) ---
    PDI_Context      context;
    PDI_Alarms       alarms;
    PDI_Measurements measurements;

    // --- Modo del dispositivo ---
    DeviceMode deviceMode;
};

// ─────────────────────────────────────────────────────────────────────────────
// Utilidades de conversión
// ─────────────────────────────────────────────────────────────────────────────

/** Convierte MachineState a string legible */
const char* machineStateToStr(MachineState state);

/** Convierte UnitMode a string legible */
const char* unitModeToStr(UnitMode mode);

/** Convierte QualitySignal a string legible */
const char* qualitySignalToStr(QualitySignal q);

/** Convierte DeviceMode a string legible */
const char* deviceModeToStr(DeviceMode mode);

/** Convierte string a MachineState */
MachineState strToMachineState(const char* str);

#endif // PDI_DATA_H
