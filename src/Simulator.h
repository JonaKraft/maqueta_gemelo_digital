/**
 * @file Simulator.h
 * @brief Generador de datos simulados coherentes para demostraciones
 *
 * En modo SIMULADOR, genera datos PDI que imitan el comportamiento
 * real de una máquina industrial: velocidades que suben/bajan,
 * contadores que incrementan, estados que cambian con transiciones
 * lógicas, y alarmas esporádicas.
 */

#ifndef SIMULATOR_H
#define SIMULATOR_H

#include <Arduino.h>
#include "PDIData.h"

class Simulator {
public:
    Simulator();

    /** Resetea el simulador a un estado inicial limpio */
    void reset();

    /**
     * Genera la siguiente iteración de datos simulados.
     * Llamar periódicamente (cada 1-2 seg).
     * @param data  Referencia a la estructura PDI a rellenar
     */
    void update(PDIData& data);

private:
    // Estado interno del simulador
    float _targetSpeed;         // Velocidad objetivo (rampa)
    float _currentSpeed;        // Velocidad actual (interpolada)
    uint32_t _cycleCounter;     // Contador de ciclos de producción
    uint32_t _stateTimer;       // Tiempo en el estado actual (ms)
    uint32_t _lastUpdateMs;
    uint32_t _stateChangeMs;    // Momento del último cambio de estado

    MachineState _simState;
    MachineState _prevState;

    // Parámetros de simulación
    static constexpr float MAX_SPEED         = 120.0f;  // RPM o unidades/min
    static constexpr float SPEED_RAMP_RATE   = 2.5f;    // Unidades por segundo
    static constexpr float DEFECT_RATE       = 0.03f;   // 3% de defectos
    static constexpr uint32_t MIN_STATE_DURATION = 8000;  // Min 8 seg en cada estado
    static constexpr uint32_t MAX_STATE_DURATION = 30000; // Max 30 seg

    /** Decide si cambiar de estado (basado en probabilidad y tiempo) */
    void maybeChangeState();

    /** Simula la rampa de velocidad */
    void updateSpeed(float deltaTime);

    /** Simula la producción (incrementa contadores) */
    void updateProduction(float deltaTime, PDIData& data);

    /** Genera mediciones simuladas coherentes */
    void updateMeasurements(PDIData& data);

    /** Genera un float aleatorio en rango [min, max] */
    float randomFloat(float minVal, float maxVal);
};

#endif // SIMULATOR_H
