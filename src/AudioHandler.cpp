/**
 * @file AudioHandler.cpp
 * @brief Implementación del manejo de audio con I2S y LittleFS
 */

#include "AudioHandler.h"

AudioHandler::AudioHandler(MqttHandler& mqtt)
    : _mqtt(mqtt)
    , _state(AudioState::IDLE)
    , _recordStartMs(0)
    , _lastRecordDuration(0)
    , _pttPressed(false)
    , _lastPttState(true) // INPUT_PULLUP → HIGH en reposo
    , _pttDebounceMs(0)
    , _playbackReady(false)
    , _volume(80)
    , _sendChunkIdx(0)
    , _sendTotalChunks(0)
    , _sendLastChunkMs(0)
{}

void AudioHandler::begin(const AudioConfig& config) {
    _config = config;

    // Configurar pin PTT con pull-up interno
    pinMode(_config.pttPin, INPUT_PULLUP);

    // Inicializar LittleFS para almacenamiento temporal de audio
    Serial.println("[Audio] Montando LittleFS..."); Serial.flush();
    if (!LittleFS.begin(true)) {
        Serial.println("[Audio] Error montando LittleFS!");
        return;
    }

    // Configurar I2S para entrada de micrófono
    Serial.println("[Audio] Configurando I2S mic..."); Serial.flush();
    setupI2SInput();

    // Configurar I2S para speaker al inicio (evita pines flotantes → chasquidos)
    Serial.println("[Audio] Configurando I2S speaker..."); Serial.flush();
    setupI2SOutput();

    // Configurar pin de habilitación de amplificador
    if (_config.ampEnablePin != 0xFF) {
        pinMode(_config.ampEnablePin, OUTPUT);
        digitalWrite(_config.ampEnablePin, LOW);
    }

    Serial.printf("[Audio] Inicializado: %dHz, %dbit, PTT=GPIO%d\n",
                  _config.sampleRate, _config.bitsPerSample, _config.pttPin);
    Serial.flush();
}

void AudioHandler::setupI2SInput() {
    i2s_config_t i2sConfig = {};
    i2sConfig.mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_RX);
    i2sConfig.sample_rate = _config.sampleRate;
    i2sConfig.bits_per_sample = I2S_BITS_PER_SAMPLE_32BIT; // INMP441 usa 24-bit en frame 32
    i2sConfig.channel_format = I2S_CHANNEL_FMT_ONLY_LEFT;
    i2sConfig.communication_format = I2S_COMM_FORMAT_STAND_I2S;
    i2sConfig.intr_alloc_flags = ESP_INTR_FLAG_LEVEL1;
    i2sConfig.dma_buf_count = 4;
    i2sConfig.dma_buf_len = 128;
    i2sConfig.use_apll = false;
    i2sConfig.tx_desc_auto_clear = false;
    i2sConfig.fixed_mclk = 0;

    i2s_pin_config_t pinConfig = {};
    pinConfig.bck_io_num = _config.i2sSckPin;
    pinConfig.ws_io_num = _config.i2sWsPin;
    pinConfig.data_in_num = _config.i2sSdPin;
    pinConfig.data_out_num = I2S_PIN_NO_CHANGE;

    esp_err_t err = i2s_driver_install(I2S_NUM_0, &i2sConfig, 0, NULL);
    if (err != ESP_OK) {
        Serial.printf("[Audio] Error instalando I2S: %d\n", err);
        return;
    }

    err = i2s_set_pin(I2S_NUM_0, &pinConfig);
    if (err != ESP_OK) {
        Serial.printf("[Audio] Error configurando pines I2S: %d\n", err);
    }

    // Parar I2S hasta que se necesite (evita conflicto DMA con I2C del display)
    i2s_stop(I2S_NUM_0);
}

void AudioHandler::update() {
    // Reproducción y envío se procesan sin leer PTT
    if (_state == AudioState::PLAYING) {
        updatePlayback();
        return;
    }
    if (_state == AudioState::SENDING) {
        updateSending();
        return;
    }

    // Leer estado del botón PTT con debounce (activo en LOW)
    bool rawReading = digitalRead(_config.pttPin) == LOW;
    uint32_t now = millis();

    // Debounce: solo aceptar cambio si el estado se mantiene estable
    if (rawReading != _lastPttState) {
        _pttDebounceMs = now;
    }
    _lastPttState = rawReading;

    if ((now - _pttDebounceMs) < PTT_DEBOUNCE_MS) {
        return;
    }

    bool pttStable = rawReading;

    switch (_state) {
        case AudioState::IDLE: {
            if (pttStable && !_pttPressed) {
                _pttPressed = true;
                startRecording();
            }
            if (!pttStable) {
                _pttPressed = false;
            }
            break;
        }

        case AudioState::RECORDING: {
            uint32_t elapsed = now - _recordStartMs;

            // Leer 32-bit del INMP441 y convertir a 16-bit (timeout corto, no bloqueante)
            size_t bytesRead = 0;
            esp_err_t err = i2s_read(I2S_NUM_0, _i2sReadBuf,
                                     I2S_READ_SAMPLES * sizeof(int32_t),
                                     &bytesRead, pdMS_TO_TICKS(50));

            if (err == ESP_OK && bytesRead > 0 && _tempFile) {
                size_t samplesRead = bytesRead / sizeof(int32_t);
                for (size_t i = 0; i < samplesRead; i++) {
                    _convertBuf[i] = (int16_t)(_i2sReadBuf[i] >> 16);
                }
                _tempFile.write((uint8_t*)_convertBuf, samplesRead * sizeof(int16_t));
            }

            if (!pttStable || elapsed >= _config.maxDurationMs) {
                _pttPressed = false;
                stopRecording();
            }
            break;
        }

        default:
            break;
    }
}

void AudioHandler::startRecording() {
    Serial.println("[Audio] Iniciando grabacion...");

    // Crear/truncar archivo temporal
    _tempFile = LittleFS.open(TEMP_AUDIO_PATH, "w");
    if (!_tempFile) {
        Serial.println("[Audio] Error abriendo archivo temporal!");
        _state = AudioState::IDLE;
        return;
    }

    // Reservar espacio para la cabecera WAV (44 bytes)
    uint8_t emptyHeader[44] = {0};
    _tempFile.write(emptyHeader, 44);

    // Arrancar I2S solo cuando se necesita (evita conflicto con display I2C)
    i2s_start(I2S_NUM_0);
    i2s_zero_dma_buffer(I2S_NUM_0);

    // Descartar datos basura acumulados en los buffers DMA
    size_t dummy;
    for (int i = 0; i < 4; i++) {
        i2s_read(I2S_NUM_0, _i2sReadBuf, I2S_READ_SAMPLES * sizeof(int32_t),
                 &dummy, pdMS_TO_TICKS(20));
    }

    _recordStartMs = millis();
    _state = AudioState::RECORDING;
}

void AudioHandler::stopRecording() {
    // Parar I2S del micrófono para liberar el bus DMA
    i2s_stop(I2S_NUM_0);

    _lastRecordDuration = millis() - _recordStartMs;
    Serial.printf("[Audio] Grabacion detenida. Duracion: %d ms\n", _lastRecordDuration);

    if (_tempFile) {
        size_t fileSize = _tempFile.size();
        uint32_t dataSize = fileSize - 44;

        _tempFile.seek(0);
        writeWavHeader(_tempFile, dataSize);
        _tempFile.close();

        Serial.printf("[Audio] Archivo WAV: %d bytes de audio\n", dataSize);
    }

    // Iniciar reproducción no-bloqueante
    playLastRecording();
}

void AudioHandler::writeWavHeader(File& file, uint32_t dataSize) {
    uint32_t sampleRate = _config.sampleRate;
    uint16_t bitsPerSample = _config.bitsPerSample;
    uint16_t numChannels = _config.channels;
    uint32_t byteRate = sampleRate * numChannels * (bitsPerSample / 8);
    uint16_t blockAlign = numChannels * (bitsPerSample / 8);
    uint32_t chunkSize = 36 + dataSize;

    // RIFF header
    file.write((const uint8_t*)"RIFF", 4);
    file.write((uint8_t*)&chunkSize, 4);
    file.write((const uint8_t*)"WAVE", 4);

    // fmt subchunk
    file.write((const uint8_t*)"fmt ", 4);
    uint32_t subchunk1Size = 16;
    file.write((uint8_t*)&subchunk1Size, 4);
    uint16_t audioFormat = 1; // PCM
    file.write((uint8_t*)&audioFormat, 2);
    file.write((uint8_t*)&numChannels, 2);
    file.write((uint8_t*)&sampleRate, 4);
    file.write((uint8_t*)&byteRate, 4);
    file.write((uint8_t*)&blockAlign, 2);
    file.write((uint8_t*)&bitsPerSample, 2);

    // data subchunk
    file.write((const uint8_t*)"data", 4);
    file.write((uint8_t*)&dataSize, 4);
}

void AudioHandler::updateSending() {
    uint32_t now = millis();

    // Esperar 50ms entre chunks para no saturar el broker
    if ((now - _sendLastChunkMs) < 50) {
        return;
    }

    if (!_sendFile || !_sendFile.available()) {
        // Envío completado
        if (_sendFile) {
            _sendFile.close();
        }
        _state = AudioState::IDLE;
        Serial.println("[Audio] Envio MQTT completado");
        return;
    }

    size_t bytesRead = _sendFile.read(_mqttChunkBuf, MQTT_CHUNK_SIZE);
    if (bytesRead > 0) {
        _mqtt.publishAudioChunk(_mqttChunkBuf, bytesRead, _sendChunkIdx, _sendTotalChunks);
        _sendChunkIdx++;
        _sendLastChunkMs = now;
    }
}

AudioState AudioHandler::getState() const {
    return _state;
}

uint32_t AudioHandler::getRecordingDurationMs() const {
    if (_state == AudioState::RECORDING) {
        return millis() - _recordStartMs;
    }
    return _lastRecordDuration;
}

// ─────────────────────────────────────────────────────────────────────────────
// Reproducción de audio (MAX98357A vía I2S_NUM_1)
// ─────────────────────────────────────────────────────────────────────────────

void AudioHandler::setupI2SOutput() {
    i2s_config_t i2sConfig = {};
    i2sConfig.mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_TX);
    i2sConfig.sample_rate = _config.sampleRate;
    i2sConfig.bits_per_sample = I2S_BITS_PER_SAMPLE_16BIT;
    i2sConfig.channel_format = I2S_CHANNEL_FMT_ONLY_LEFT;
    i2sConfig.communication_format = I2S_COMM_FORMAT_STAND_I2S;
    i2sConfig.intr_alloc_flags = ESP_INTR_FLAG_LEVEL1;
    i2sConfig.dma_buf_count = 8;
    i2sConfig.dma_buf_len = 256;
    i2sConfig.use_apll = false;
    i2sConfig.tx_desc_auto_clear = true;

    i2s_pin_config_t pinConfig = {};
    pinConfig.bck_io_num = _config.i2sSpkSckPin;
    pinConfig.ws_io_num = _config.i2sSpkWsPin;
    pinConfig.data_out_num = _config.i2sSpkSdPin;
    pinConfig.data_in_num = I2S_PIN_NO_CHANGE;

    esp_err_t err = i2s_driver_install(I2S_NUM_1, &i2sConfig, 0, NULL);
    if (err != ESP_OK) {
        Serial.printf("[Audio] Error instalando I2S_NUM_1 (speaker): %d\n", err);
        return;
    }

    err = i2s_set_pin(I2S_NUM_1, &pinConfig);
    if (err != ESP_OK) {
        Serial.printf("[Audio] Error configurando pines speaker: %d\n", err);
        return;
    }

    i2s_zero_dma_buffer(I2S_NUM_1);
    i2s_stop(I2S_NUM_1); // Parado hasta que se necesite
    _playbackReady = true;
    Serial.printf("[Audio] Speaker I2S inicializado: BCLK=%d, LRC=%d, DIN=%d\n",
                  _config.i2sSpkSckPin, _config.i2sSpkWsPin, _config.i2sSpkSdPin);
}

void AudioHandler::beginPlayback() {
    if (!_playbackReady) {
        setupI2SOutput();
    }
}

void AudioHandler::playLastRecording() {
    if (!_playbackReady) {
        Serial.println("[Audio] Speaker no inicializado");
        _state = AudioState::IDLE;
        return;
    }

    _playbackFile = LittleFS.open(TEMP_AUDIO_PATH, "r");
    if (!_playbackFile) {
        Serial.println("[Audio] No hay grabacion para reproducir");
        _state = AudioState::IDLE;
        return;
    }

    size_t fileSize = _playbackFile.size();
    if (fileSize <= 44) {
        Serial.println("[Audio] Archivo de audio vacio");
        _playbackFile.close();
        _state = AudioState::IDLE;
        return;
    }

    // Saltar cabecera WAV (44 bytes)
    _playbackFile.seek(44);

    _state = AudioState::PLAYING;
    Serial.printf("[Audio] Reproduciendo %d bytes...\n", fileSize - 44);

    if (_config.ampEnablePin != 0xFF) {
        digitalWrite(_config.ampEnablePin, HIGH);
    }
    i2s_start(I2S_NUM_1);
}

void AudioHandler::updatePlayback() {
    if (!_playbackFile || !_playbackFile.available()) {
        // Reproducción completada: drenar DMA y parar
        i2s_zero_dma_buffer(I2S_NUM_1);
        i2s_stop(I2S_NUM_1);

        if (_config.ampEnablePin != 0xFF) {
            digitalWrite(_config.ampEnablePin, LOW);
        }

        if (_playbackFile) {
            _playbackFile.close();
        }

        Serial.println("[Audio] Reproduccion completada");

        // Enviar por MQTT si está conectado
        if (_mqtt.isConnected()) {
            _sendFile = LittleFS.open(TEMP_AUDIO_PATH, "r");
            if (_sendFile) {
                size_t fileSize = _sendFile.size();
                _sendTotalChunks = (fileSize + MQTT_CHUNK_SIZE - 1) / MQTT_CHUNK_SIZE;
                _sendChunkIdx = 0;
                _sendLastChunkMs = 0;
                _state = AudioState::SENDING;
                Serial.printf("[Audio] Enviando %d bytes en %d chunks...\n", fileSize, _sendTotalChunks);
                return;
            }
        }

        _state = AudioState::IDLE;
        return;
    }

    // Escribir un chunk al speaker (timeout corto para no bloquear)
    size_t bytesRead = _playbackFile.read(_playbackBuf, PLAYBACK_CHUNK_SIZE);
    if (bytesRead > 0) {
        // Escalar samples PCM según volumen (aritmética entera, sin float)
        if (_volume < 100) {
            int16_t* samples = (int16_t*)_playbackBuf;
            size_t numSamples = bytesRead / sizeof(int16_t);
            for (size_t i = 0; i < numSamples; i++) {
                samples[i] = (int16_t)((int32_t)samples[i] * _volume / 100);
            }
        }
        size_t bytesWritten;
        i2s_write(I2S_NUM_1, _playbackBuf, bytesRead, &bytesWritten, pdMS_TO_TICKS(50));
    }
}

void AudioHandler::setVolume(uint8_t vol) {
    _volume = vol > 100 ? 100 : vol;
}

uint8_t AudioHandler::getVolume() const {
    return _volume;
}

void AudioHandler::stopPlayback() {
    if (_playbackReady) {
        i2s_zero_dma_buffer(I2S_NUM_1);
        i2s_stop(I2S_NUM_1);
    }
    if (_config.ampEnablePin != 0xFF) {
        digitalWrite(_config.ampEnablePin, LOW);
    }
    if (_playbackFile) {
        _playbackFile.close();
    }
    if (_sendFile) {
        _sendFile.close();
    }
    _state = AudioState::IDLE;
    Serial.println("[Audio] Reproduccion detenida");
}
