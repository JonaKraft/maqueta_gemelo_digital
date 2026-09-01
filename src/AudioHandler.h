/**
 * @file AudioHandler.h
 * @brief Manejo de audio: grabación Push-to-Talk y reproducción (stub)
 *
 * Graba audio desde un micrófono electret usando I2S (recomendado sobre ADC
 * puro para calidad y DMA). El audio se graba mientras se mantiene presionado
 * el botón PTT, con un límite de 20 segundos.
 *
 * Si la placa tiene PSRAM, el buffer se aloja allí. Si no, se escribe
 * incrementalmente a LittleFS para evitar agotar la SRAM.
 *
 * El audio se empaqueta como WAV PCM y se envía fragmentado por MQTT.
 *
 * La reproducción (I2S out a MAX98357A) queda como stub para implementación futura.
 */

#ifndef AUDIO_HANDLER_H
#define AUDIO_HANDLER_H

#include <Arduino.h>
#include <driver/i2s.h>
#include <LittleFS.h>
#include "MqttHandler.h"

/** Configuración de audio */
struct AudioConfig {
    uint32_t sampleRate   = 16000;   // 16 kHz
    uint8_t  bitsPerSample = 16;     // 16 bits
    uint8_t  channels      = 1;      // Mono
    uint32_t maxDurationMs = 20000;  // 20 segundos máximo
    uint8_t  pttPin        = 0;      // Pin del botón Push-to-Talk

    // Pines I2S para micrófono (INMP441 o similar)
    uint8_t i2sWsPin   = 15;   // Word Select (LRCLK)
    uint8_t i2sSckPin  = 14;   // Bit Clock (BCLK)
    uint8_t i2sSdPin   = 16;   // Serial Data (DOUT del mic)

    // Pines I2S para speaker (MAX98357A)
    uint8_t i2sSpkWsPin  = 8;
    uint8_t i2sSpkSckPin = 7;
    uint8_t i2sSpkSdPin  = 18;

    // Pin de habilitación de amplificador (BC337, activo HIGH)
    uint8_t ampEnablePin = 0xFF;  // 0xFF = no configurado
};

/** Estados del módulo de audio */
enum class AudioState : uint8_t {
    IDLE       = 0,
    RECORDING  = 1,
    SENDING    = 2,
    PLAYING    = 3   // Futuro
};

class AudioHandler {
public:
    AudioHandler(MqttHandler& mqtt);

    /**
     * Inicializa el periférico I2S y el pin PTT.
     * @param config  Configuración de audio (pines, sample rate, etc.)
     */
    void begin(const AudioConfig& config);

    /**
     * Actualización periódica. Gestiona la grabación según el estado del PTT.
     * No bloqueante.
     */
    void update();

    /** Estado actual del módulo de audio */
    AudioState getState() const;

    /** Duración actual o de la última grabación en ms */
    uint32_t getRecordingDurationMs() const;

    /** Inicializa I2S para salida de audio (MAX98357A) */
    void beginPlayback();

    /** Reproduce la ultima grabacion almacenada en LittleFS */
    void playLastRecording();

    /** Detiene la reproducción */
    void stopPlayback();

    /** Ajusta el volumen de reproducción (0 = mudo, 100 = máximo) */
    void setVolume(uint8_t vol);

    /** Obtiene el volumen actual (0-100) */
    uint8_t getVolume() const;

private:
    MqttHandler& _mqtt;
    AudioConfig _config;
    AudioState _state;

    // Grabación
    uint32_t _recordStartMs;
    uint32_t _lastRecordDuration;
    bool _pttPressed;
    bool _lastPttState;
    uint32_t _pttDebounceMs;
    File _tempFile;            // Archivo temporal en LittleFS

    static constexpr uint32_t PTT_DEBOUNCE_MS = 100;

    // Buffer para lectura I2S (32-bit porque INMP441 envía 24-bit en frame de 32)
    static constexpr size_t I2S_READ_SAMPLES = 256;
    int32_t _i2sReadBuf[I2S_READ_SAMPLES];
    int16_t _convertBuf[I2S_READ_SAMPLES];

    bool _playbackReady;
    uint8_t _volume;  // 0-100, escala lineal para samples PCM

    // Reproducción no-bloqueante
    File _playbackFile;
    static constexpr size_t PLAYBACK_CHUNK_SIZE = 512;
    uint8_t _playbackBuf[PLAYBACK_CHUNK_SIZE];

    // Envío MQTT no-bloqueante
    File _sendFile;
    uint16_t _sendChunkIdx;
    uint16_t _sendTotalChunks;
    uint32_t _sendLastChunkMs;
    static constexpr size_t MQTT_CHUNK_SIZE = 4096;
    uint8_t _mqttChunkBuf[MQTT_CHUNK_SIZE];

    static constexpr const char* TEMP_AUDIO_PATH = "/audio_temp.raw";

    /** Configura el periférico I2S para captura */
    void setupI2SInput();

    /** Configura el periférico I2S para salida al speaker */
    void setupI2SOutput();

    /** Inicia una grabación */
    void startRecording();

    /** Detiene la grabación y prepara reproducción */
    void stopRecording();

    /** Escribe la cabecera WAV en el archivo */
    void writeWavHeader(File& file, uint32_t dataSize);

    /** Actualiza reproducción no-bloqueante (un chunk por llamada) */
    void updatePlayback();

    /** Actualiza envío MQTT no-bloqueante (un chunk por llamada) */
    void updateSending();
};

#endif // AUDIO_HANDLER_H
