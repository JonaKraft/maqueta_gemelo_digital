/**
 * @file EncoderInput.h
 * @brief Manejo del encoder rotativo con pulsador
 *
 * Gestiona la lectura del encoder rotativo (CLK/DT) y el botón integrado
 * con debounce por software. Genera eventos de rotación y pulsación
 * que consume el módulo de DisplayMenu.
 */

#ifndef ENCODER_INPUT_H
#define ENCODER_INPUT_H

#include <Arduino.h>

/** Eventos generados por el encoder */
enum class EncoderEvent : uint8_t {
    NONE          = 0,
    ROTATE_CW     = 1,   // Giro horario (incremento)
    ROTATE_CCW    = 2,   // Giro antihorario (decremento)
    BUTTON_PRESS  = 3,   // Pulsación corta
    BUTTON_LONG   = 4    // Pulsación larga (>1 segundo)
};

class EncoderInput {
public:
    /**
     * @param pinCLK  Pin de señal CLK del encoder
     * @param pinDT   Pin de señal DT del encoder
     * @param pinSW   Pin del pulsador integrado
     */
    EncoderInput(uint8_t pinCLK, uint8_t pinDT, uint8_t pinSW);

    /** Configura los pines y adjunta interrupciones */
    void begin();

    /**
     * Lee el estado actual y retorna el evento pendiente (si existe).
     * Llamar desde el loop o tarea periódica (no bloqueante).
     */
    EncoderEvent read();

    /** Obtiene la posición acumulada del encoder */
    int32_t getPosition() const;

    /** Resetea la posición a cero */
    void resetPosition();

private:
    uint8_t _pinCLK;
    uint8_t _pinDT;
    uint8_t _pinSW;

    volatile int32_t _position;
    volatile int32_t _lastPosition;

    // Estado del botón con debounce
    bool     _buttonState;
    bool     _lastButtonReading;
    uint32_t _lastDebounceTime;
    uint32_t _buttonPressStart;
    bool     _buttonHandled;

    // Estado del encoder
    volatile uint8_t _lastEncoded;

    static constexpr uint32_t DEBOUNCE_MS     = 50;
    static constexpr uint32_t LONG_PRESS_MS   = 1000;

    // ISR estática y puntero a instancia para la interrupción
    static void IRAM_ATTR isrHandler();
    static EncoderInput* _instance;

    void handleEncoder();
};

#endif // ENCODER_INPUT_H
