/**
 * @file Simulator.cpp
 * @brief Implementación del generador de datos simulados coherentes
 */

#include "Simulator.h"

Simulator::Simulator()
    : _targetSpeed(0)
    , _currentSpeed(0)
    , _cycleCounter(0)
    , _stateTimer(0)
    , _lastUpdateMs(0)
    , _stateChangeMs(0)
    , _simState(MachineState::IDLE)
    , _prevState(MachineState::IDLE)
{}

void Simulator::reset() {
    _targetSpeed = 0;
    _currentSpeed = 0;
    _cycleCounter = 0;
    _stateTimer = 0;
    _lastUpdateMs = millis();
    _stateChangeMs = millis();
    _simState = MachineState::IDLE;
    _prevState = MachineState::IDLE;
}

void Simulator::update(PDIData& data) {
    uint32_t now = millis();
    float deltaTime = (now - _lastUpdateMs) / 1000.0f;
    if (deltaTime < 0.1f) return; // Mínimo 100ms entre actualizaciones
    _lastUpdateMs = now;

    // Actualizar tiempo en estado actual
    _stateTimer = now - _stateChangeMs;

    // Lógica de cambio de estado
    maybeChangeState();

    // Actualizar velocidad según el estado
    switch (_simState) {
        case MachineState::EXECUTING:
            _targetSpeed = MAX_SPEED * randomFloat(0.7f, 1.0f);
            break;
        case MachineState::IDLE:
            _targetSpeed = MAX_SPEED * randomFloat(0.0f, 0.15f);
            break;
        case MachineState::STOPPED:
        case MachineState::HELD:
        case MachineState::ABORTED:
            _targetSpeed = 0;
            break;
    }

    updateSpeed(deltaTime);
    updateProduction(deltaTime, data);
    updateMeasurements(data);

    // Llenar la estructura PDI
    data.equipmentName = "SIM-ESP32-GD";
    data.assetId = "SIM-ASSET-001";
    data.timestamp = now / 1000; // Simplificado: segundos desde boot

    data.status.stateCurrent = _simState;
    data.status.statePrevious = _prevState;
    data.status.stateDuration = _stateTimer / 1000;
    data.status.machSpeed = MAX_SPEED;
    data.status.curMachSpeed = _currentSpeed;

    // Modo según el estado
    if (_simState == MachineState::EXECUTING) {
        data.status.unitModeCurrent = UnitMode::AUTO;
    } else if (_simState == MachineState::IDLE) {
        data.status.unitModeCurrent = UnitMode::MANUAL;
    } else {
        data.status.unitModeCurrent = UnitMode::SETUP;
    }

    // Calidad basada en la velocidad actual
    if (_currentSpeed > MAX_SPEED * 0.5f) {
        data.qualitySignal = QualitySignal::OK;
    } else if (_currentSpeed > MAX_SPEED * 0.2f) {
        data.qualitySignal = QualitySignal::UNCERTAIN;
    } else {
        data.qualitySignal = (_simState == MachineState::EXECUTING)
                             ? QualitySignal::BAD : QualitySignal::OK;
    }

    data.connectionStatus = true;
    data.deviceMode = DeviceMode::SIMULATOR;

    // Contexto simulado
    data.context.orderNo = "ORD-SIM-" + String((_cycleCounter / 100) + 1);
    data.context.productCode = "PROD-A1";
    data.context.recipeId = "RCP-001";
    data.context.lotBatch = "LOT-" + String(now / 60000);
    data.context.lineId = "LINE-01";

    // Alarmas esporádicas
    if (_simState == MachineState::STOPPED && random(100) < 20) {
        data.alarms.alarmActive = true;
        data.alarms.alarmCode = "E-" + String(random(100, 999));
        data.alarms.alarmText = "Falla simulada";
        data.alarms.stopReasonCode = "SR-" + String(random(10, 50));
        data.alarms.stopReasonDesc = "Parada planificada";
    } else {
        data.alarms.alarmActive = false;
        data.alarms.alarmCode = "";
        data.alarms.alarmText = "";
    }
}

void Simulator::maybeChangeState() {
    // No cambiar si no se alcanzó la duración mínima
    if (_stateTimer < MIN_STATE_DURATION) return;

    // Probabilidad de cambio aumenta con el tiempo
    float changeProbability = (float)(_stateTimer - MIN_STATE_DURATION)
                              / (float)(MAX_STATE_DURATION - MIN_STATE_DURATION);
    changeProbability = constrain(changeProbability, 0.0f, 1.0f);

    if (randomFloat(0, 1.0f) > changeProbability) return;

    // Transiciones lógicas (no cualquier estado a cualquier otro)
    _prevState = _simState;
    _stateChangeMs = millis();

    switch (_simState) {
        case MachineState::IDLE:
            // Desde Idle: puede arrancar o detenerse
            _simState = (random(100) < 80) ? MachineState::EXECUTING
                                           : MachineState::STOPPED;
            break;

        case MachineState::EXECUTING:
            // Desde Ejecutando: puede ir a Idle o a Parado
            if (random(100) < 60) {
                _simState = MachineState::IDLE;
            } else {
                _simState = MachineState::STOPPED;
            }
            break;

        case MachineState::STOPPED:
            // Desde Parado: siempre va a Idle primero
            _simState = MachineState::IDLE;
            break;

        case MachineState::HELD:
        case MachineState::ABORTED:
            _simState = MachineState::IDLE;
            break;
    }

    Serial.printf("[Sim] Cambio de estado: %s -> %s\n",
                  machineStateToStr(_prevState),
                  machineStateToStr(_simState));
}

void Simulator::updateSpeed(float deltaTime) {
    float diff = _targetSpeed - _currentSpeed;
    float change = SPEED_RAMP_RATE * deltaTime;

    if (abs(diff) < change) {
        _currentSpeed = _targetSpeed;
    } else if (diff > 0) {
        _currentSpeed += change;
    } else {
        _currentSpeed -= change * 2.0f; // Desacelera más rápido
    }

    // Agregar un poco de ruido realista
    _currentSpeed += randomFloat(-0.5f, 0.5f);
    _currentSpeed = constrain(_currentSpeed, 0.0f, MAX_SPEED * 1.05f);
}

void Simulator::updateProduction(float deltaTime, PDIData& data) {
    if (_simState == MachineState::EXECUTING && _currentSpeed > 10.0f) {
        // Producir piezas proporcional a la velocidad
        float piecesPerSec = _currentSpeed / 60.0f;
        _cycleCounter += (uint32_t)(piecesPerSec * deltaTime);

        data.admin.prodProcessedCount = _cycleCounter;
        data.admin.prodDefectiveCount = (uint32_t)(_cycleCounter * DEFECT_RATE);
    } else {
        data.admin.prodProcessedCount = _cycleCounter;
        data.admin.prodDefectiveCount = (uint32_t)(_cycleCounter * DEFECT_RATE);
    }
}

void Simulator::updateMeasurements(PDIData& data) {
    float speedRatio = _currentSpeed / MAX_SPEED;

    // Potencia proporcional a la velocidad con ruido
    data.measurements.power = speedRatio * 2200.0f + randomFloat(-50, 50);
    data.measurements.power = max(0.0f, data.measurements.power);

    // Corriente correlacionada con potencia (P = V*I, ~220V)
    data.measurements.current = data.measurements.power / 220.0f + randomFloat(-0.2f, 0.2f);
    data.measurements.current = max(0.0f, data.measurements.current);

    // Presión de aire relativamente estable
    data.measurements.airPressure = 6.0f + randomFloat(-0.3f, 0.3f);
    if (_simState == MachineState::EXECUTING) {
        data.measurements.airPressure -= speedRatio * 0.5f; // Consume más aire
    }
}

float Simulator::randomFloat(float minVal, float maxVal) {
    return minVal + (float)random(10000) / 10000.0f * (maxVal - minVal);
}
