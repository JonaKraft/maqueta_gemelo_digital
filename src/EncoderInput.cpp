/**
 * @file EncoderInput.cpp
 * @brief Implementación del encoder rotativo con interrupción y debounce
 */

#include "EncoderInput.h"

// Puntero estático para la ISR
EncoderInput* EncoderInput::_instance = nullptr;

EncoderInput::EncoderInput(uint8_t pinCLK, uint8_t pinDT, uint8_t pinSW)
    : _pinCLK(pinCLK)
    , _pinDT(pinDT)
    , _pinSW(pinSW)
    , _position(0)
    , _lastPosition(0)
    , _buttonState(HIGH)
    , _lastButtonReading(HIGH)
    , _lastDebounceTime(0)
    , _buttonPressStart(0)
    , _buttonHandled(false)
    , _lastEncoded(0)
{
    _instance = this;
}

void EncoderInput::begin() {
    pinMode(_pinCLK, INPUT_PULLUP);
    pinMode(_pinDT, INPUT_PULLUP);
    pinMode(_pinSW, INPUT_PULLUP);

    // Lectura inicial del encoder
    uint8_t a = digitalRead(_pinCLK);
    uint8_t b = digitalRead(_pinDT);
    _lastEncoded = (a << 1) | b;

    // Interrupción en ambos flancos de CLK y DT
    attachInterrupt(digitalPinToInterrupt(_pinCLK), isrHandler, CHANGE);
    attachInterrupt(digitalPinToInterrupt(_pinDT), isrHandler, CHANGE);

    Serial.println("[Encoder] Inicializado");
}

void IRAM_ATTR EncoderInput::isrHandler() {
    if (_instance) {
        _instance->handleEncoder();
    }
}

void EncoderInput::handleEncoder() {
    uint8_t a = digitalRead(_pinCLK);
    uint8_t b = digitalRead(_pinDT);
    uint8_t encoded = (a << 1) | b;

    // Tabla de decodificación Gray code para encoder cuadratura
    uint8_t sum = (_lastEncoded << 2) | encoded;

    // Transiciones válidas que indican dirección
    switch (sum) {
        case 0b0001: case 0b0111: case 0b1110: case 0b1000:
            _position++;
            break;
        case 0b0010: case 0b1011: case 0b1101: case 0b0100:
            _position--;
            break;
    }

    _lastEncoded = encoded;
}

EncoderEvent EncoderInput::read() {
    // --- Verificar rotación ---
    int32_t pos = _position;
    int32_t diff = pos - _lastPosition;

    if (diff >= 2) {
        // Se requieren 2 pasos para un "click" mecánico (detent)
        _lastPosition = pos;
        return EncoderEvent::ROTATE_CW;
    }
    if (diff <= -2) {
        _lastPosition = pos;
        return EncoderEvent::ROTATE_CCW;
    }

    // --- Verificar botón con debounce ---
    bool reading = digitalRead(_pinSW);
    uint32_t now = millis();

    if (reading != _lastButtonReading) {
        _lastDebounceTime = now;
    }
    _lastButtonReading = reading;

    if ((now - _lastDebounceTime) > DEBOUNCE_MS) {
        if (reading != _buttonState) {
            _buttonState = reading;

            if (_buttonState == LOW) {
                // Botón recién presionado
                _buttonPressStart = now;
                _buttonHandled = false;
            } else {
                // Botón recién liberado
                if (!_buttonHandled) {
                    uint32_t duration = now - _buttonPressStart;
                    if (duration >= LONG_PRESS_MS) {
                        return EncoderEvent::BUTTON_LONG;
                    } else {
                        return EncoderEvent::BUTTON_PRESS;
                    }
                }
            }
        }

        // Detectar pulsación larga mientras sigue presionado
        if (_buttonState == LOW && !_buttonHandled) {
            if ((now - _buttonPressStart) >= LONG_PRESS_MS) {
                _buttonHandled = true;
                return EncoderEvent::BUTTON_LONG;
            }
        }
    }

    return EncoderEvent::NONE;
}

int32_t EncoderInput::getPosition() const {
    return _position;
}

void EncoderInput::resetPosition() {
    _position = 0;
    _lastPosition = 0;
}
