/**
 * @file LedController.cpp
 * @brief Implementación del controlador de LED RGB (PWM o NeoPixel)
 */

#include "LedController.h"

// ─────────────────────────────────────────────────────────────────────────────
// Constructores
// ─────────────────────────────────────────────────────────────────────────────

#ifdef USE_NEOPIXEL_LED

LedController::LedController(uint8_t pin, uint8_t numLeds)
    : _r(0), _g(0), _b(0)
    , _pattern(LedPattern::SOLID)
    , _lastUpdateMs(0)
    , _blinkState(false)
    , _breatheLevel(0)
    , _breatheUp(true)
    , _strip(numLeds, pin, NEO_GRB + NEO_KHZ800)
    , _pin(pin)
{}

#else

LedController::LedController(uint8_t pinR, uint8_t pinG, uint8_t pinB, bool commonAnode)
    : _r(0), _g(0), _b(0)
    , _pattern(LedPattern::SOLID)
    , _lastUpdateMs(0)
    , _blinkState(false)
    , _breatheLevel(0)
    , _breatheUp(true)
    , _pinR(pinR), _pinG(pinG), _pinB(pinB)
    , _commonAnode(commonAnode)
{}

#endif

// ─────────────────────────────────────────────────────────────────────────────
// Inicialización
// ─────────────────────────────────────────────────────────────────────────────

void LedController::begin() {
#ifdef USE_NEOPIXEL_LED
    _strip.begin();
    _strip.setBrightness(128);
    _strip.show();
    Serial.println("[LED] NeoPixel inicializado");
#else
    // Configurar canales LEDC para PWM en ESP32-S3 (API v2.x)
    ledcSetup(LEDC_CH_R, LEDC_FREQ, LEDC_RES);
    ledcSetup(LEDC_CH_G, LEDC_FREQ, LEDC_RES);
    ledcSetup(LEDC_CH_B, LEDC_FREQ, LEDC_RES);
    ledcAttachPin(_pinR, LEDC_CH_R);
    ledcAttachPin(_pinG, LEDC_CH_G);
    ledcAttachPin(_pinB, LEDC_CH_B);
    Serial.println("[LED] PWM RGB inicializado");
#endif
    off();
}

// ─────────────────────────────────────────────────────────────────────────────
// Control de color y patrón
// ─────────────────────────────────────────────────────────────────────────────

void LedController::setColor(uint8_t r, uint8_t g, uint8_t b) {
    _r = r;
    _g = g;
    _b = b;
}

void LedController::setPattern(LedPattern pattern) {
    if (_pattern != pattern) {
        _pattern = pattern;
        _blinkState = false;
        _breatheLevel = 0;
        _breatheUp = true;
        _lastUpdateMs = millis();
    }
}

void LedController::updateFromState(MachineState state) {
    switch (state) {
        case MachineState::EXECUTING:
            setColor(0, 255, 0);       // Verde
            setPattern(LedPattern::SOLID);
            break;
        case MachineState::STOPPED:
            setColor(255, 0, 0);       // Rojo
            setPattern(LedPattern::SOLID);
            break;
        case MachineState::IDLE:
            setColor(255, 200, 0);     // Amarillo
            setPattern(LedPattern::BREATHE);
            break;
        case MachineState::HELD:
            setColor(0, 0, 255);       // Azul
            setPattern(LedPattern::BLINK);
            break;
        case MachineState::ABORTED:
            setColor(255, 0, 0);       // Rojo
            setPattern(LedPattern::FAST_BLINK);
            break;
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// Actualización periódica (no bloqueante)
// ─────────────────────────────────────────────────────────────────────────────

void LedController::update() {
    uint32_t now = millis();

    switch (_pattern) {
        case LedPattern::SOLID:
            applyColor(_r, _g, _b);
            break;

        case LedPattern::BLINK:
            if (now - _lastUpdateMs >= 500) {
                _lastUpdateMs = now;
                _blinkState = !_blinkState;
            }
            if (_blinkState) {
                applyColor(_r, _g, _b);
            } else {
                applyColor(0, 0, 0);
            }
            break;

        case LedPattern::FAST_BLINK:
            if (now - _lastUpdateMs >= 125) {
                _lastUpdateMs = now;
                _blinkState = !_blinkState;
            }
            if (_blinkState) {
                applyColor(_r, _g, _b);
            } else {
                applyColor(0, 0, 0);
            }
            break;

        case LedPattern::BREATHE:
            if (now - _lastUpdateMs >= 15) {
                _lastUpdateMs = now;
                if (_breatheUp) {
                    _breatheLevel += 3;
                    if (_breatheLevel >= 255) {
                        _breatheLevel = 255;
                        _breatheUp = false;
                    }
                } else {
                    if (_breatheLevel < 3) {
                        _breatheLevel = 0;
                        _breatheUp = true;
                    } else {
                        _breatheLevel -= 3;
                    }
                }
            }
            {
                float factor = _breatheLevel / 255.0f;
                applyColor((uint8_t)(_r * factor),
                           (uint8_t)(_g * factor),
                           (uint8_t)(_b * factor));
            }
            break;

        case LedPattern::OFF:
            applyColor(0, 0, 0);
            break;
    }
}

void LedController::off() {
    _pattern = LedPattern::OFF;
    applyColor(0, 0, 0);
}

// ─────────────────────────────────────────────────────────────────────────────
// Escritura al hardware
// ─────────────────────────────────────────────────────────────────────────────

void LedController::applyColor(uint8_t r, uint8_t g, uint8_t b) {
#ifdef USE_NEOPIXEL_LED
    _strip.setPixelColor(0, _strip.Color(r, g, b));
    _strip.show();
#else
    if (_commonAnode) {
        r = 255 - r;
        g = 255 - g;
        b = 255 - b;
    }
    ledcWrite(LEDC_CH_R, r);
    ledcWrite(LEDC_CH_G, g);
    ledcWrite(LEDC_CH_B, b);
#endif
}
