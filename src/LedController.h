/**
 * @file LedController.h
 * @brief Control del LED RGB indicador de estado
 *
 * Abstrae la lógica del LED RGB para que funcione tanto con:
 * - LED RGB común (ánodo/cátodo común) controlado por PWM (ledc)
 * - LED direccionable WS2812/NeoPixel
 *
 * El tipo se selecciona en compilación con USE_NEOPIXEL_LED.
 */

#ifndef LED_CONTROLLER_H
#define LED_CONTROLLER_H

#include <Arduino.h>
#include "PDIData.h"

// Definido en platformio.ini build_flags (-DUSE_NEOPIXEL_LED) para DevKitC-1
// Para LED PWM discreto: quitar el flag de build_flags

#ifdef USE_NEOPIXEL_LED
#include <Adafruit_NeoPixel.h>
#endif

/** Patrones de parpadeo */
enum class LedPattern : uint8_t {
    SOLID     = 0,   // Color fijo
    BLINK     = 1,   // Parpadeo lento (1 Hz)
    FAST_BLINK = 2,  // Parpadeo rápido (4 Hz)
    BREATHE   = 3,   // Respiración suave (fade in/out)
    OFF       = 4    // Apagado
};

class LedController {
public:
#ifdef USE_NEOPIXEL_LED
    /**
     * Constructor para NeoPixel
     * @param pin      Pin de datos del WS2812
     * @param numLeds  Cantidad de LEDs en la cadena (normalmente 1)
     */
    LedController(uint8_t pin, uint8_t numLeds = 1);
#else
    /**
     * Constructor para LED RGB PWM
     * @param pinR  Pin del canal rojo
     * @param pinG  Pin del canal verde
     * @param pinB  Pin del canal azul
     * @param commonAnode  true si es ánodo común (invertir lógica)
     */
    LedController(uint8_t pinR, uint8_t pinG, uint8_t pinB, bool commonAnode = false);
#endif

    /** Inicializa los pines o la librería NeoPixel */
    void begin();

    /**
     * Establece el color base del LED
     * @param r  Rojo (0-255)
     * @param g  Verde (0-255)
     * @param b  Azul (0-255)
     */
    void setColor(uint8_t r, uint8_t g, uint8_t b);

    /** Establece el patrón de animación */
    void setPattern(LedPattern pattern);

    /**
     * Actualiza el estado visual del LED según el estado de la máquina.
     * Mapeo: EXECUTING→Verde, STOPPED→Rojo, IDLE→Amarillo
     */
    void updateFromState(MachineState state);

    /**
     * Actualización periódica de animaciones (llamar desde loop/tarea).
     * No bloqueante, usa millis() internamente.
     */
    void update();

    /** Apaga el LED inmediatamente */
    void off();

private:
    uint8_t _r, _g, _b;       // Color base actual
    LedPattern _pattern;
    uint32_t _lastUpdateMs;
    bool _blinkState;
    uint8_t _breatheLevel;
    bool _breatheUp;

#ifdef USE_NEOPIXEL_LED
    Adafruit_NeoPixel _strip;
    uint8_t _pin;
#else
    uint8_t _pinR, _pinG, _pinB;
    bool _commonAnode;
    // Canales LEDC para PWM (API v2.x: ledcSetup + ledcAttachPin)
    static constexpr uint8_t LEDC_CH_R = 0;
    static constexpr uint8_t LEDC_CH_G = 1;
    static constexpr uint8_t LEDC_CH_B = 2;
    static constexpr double  LEDC_FREQ = 5000;
    static constexpr uint8_t LEDC_RES  = 8;
#endif

    /** Aplica el color actual al hardware (con patrón y brillo) */
    void applyColor(uint8_t r, uint8_t g, uint8_t b);
};

#endif // LED_CONTROLLER_H
