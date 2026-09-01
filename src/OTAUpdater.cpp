/**
 * @file OTAUpdater.cpp
 * @brief Implementación de la actualización OTA por HTTP
 */

#include "OTAUpdater.h"

OTAUpdater::OTAUpdater(ConfigManager& configMgr)
    : _configMgr(configMgr)
    , _progressCb(nullptr)
    , _updating(false)
    , _progress(0)
{}

OTAResult OTAUpdater::startUpdate(const String& firmwarePath) {
    if (_updating) return OTAResult::IN_PROGRESS;

    const DeviceConfig& cfg = _configMgr.config();

    // Construir URL completa del firmware
    String url = cfg.otaUrl;
    if (cfg.otaPort != 80 && cfg.otaPort != 443) {
        // Insertar puerto si no es estándar y no está ya en la URL
        int protoEnd = url.indexOf("://");
        if (protoEnd > 0) {
            int pathStart = url.indexOf('/', protoEnd + 3);
            if (pathStart < 0) {
                url += ":" + String(cfg.otaPort);
            }
        }
    }
    if (!firmwarePath.startsWith("/")) url += "/";
    url += firmwarePath;

    Serial.printf("[OTA] Descargando firmware desde: %s\n", url.c_str());
    _updating = true;
    _progress = 0;
    _lastError = "";

    HTTPClient http;
    http.begin(url);
    http.setTimeout(30000); // 30 seg timeout

    int httpCode = http.GET();
    if (httpCode != HTTP_CODE_OK) {
        _lastError = "HTTP error: " + String(httpCode);
        Serial.printf("[OTA] %s\n", _lastError.c_str());
        http.end();
        _updating = false;
        return OTAResult::HTTP_ERROR;
    }

    int contentLength = http.getSize();
    if (contentLength <= 0) {
        _lastError = "Contenido vacio o desconocido";
        Serial.printf("[OTA] %s\n", _lastError.c_str());
        http.end();
        _updating = false;
        return OTAResult::HTTP_ERROR;
    }

    Serial.printf("[OTA] Tamano del firmware: %d bytes\n", contentLength);

    // Iniciar proceso de escritura en la partición OTA
    if (!Update.begin(contentLength)) {
        _lastError = "No hay espacio para OTA: " + String(Update.errorString());
        Serial.printf("[OTA] %s\n", _lastError.c_str());
        http.end();
        _updating = false;
        return OTAResult::WRITE_ERROR;
    }

    // Leer y escribir el firmware en bloques
    WiFiClient* stream = http.getStreamPtr();
    uint8_t buf[4096];
    size_t written = 0;

    while (written < (size_t)contentLength) {
        size_t available = stream->available();
        if (available == 0) {
            // Esperar datos con timeout
            uint32_t waitStart = millis();
            while (!stream->available() && (millis() - waitStart < 10000)) {
                delay(1);
            }
            if (!stream->available()) {
                _lastError = "Timeout esperando datos";
                Update.abort();
                http.end();
                _updating = false;
                return OTAResult::HTTP_ERROR;
            }
            continue;
        }

        size_t toRead = min(available, sizeof(buf));
        size_t bytesRead = stream->readBytes(buf, toRead);

        if (bytesRead > 0) {
            size_t bytesWritten = Update.write(buf, bytesRead);
            if (bytesWritten != bytesRead) {
                _lastError = "Error de escritura: " + String(Update.errorString());
                Update.abort();
                http.end();
                _updating = false;
                return OTAResult::WRITE_ERROR;
            }
            written += bytesWritten;
            _progress = (uint8_t)((written * 100) / contentLength);

            if (_progressCb) {
                _progressCb(written, contentLength);
            }
        }
    }

    http.end();

    // Finalizar y verificar
    if (!Update.end(true)) {
        _lastError = "Verificacion fallida: " + String(Update.errorString());
        Serial.printf("[OTA] %s\n", _lastError.c_str());
        _updating = false;
        return OTAResult::VERIFY_ERROR;
    }

    Serial.println("[OTA] Actualizacion exitosa! Reiniciando...");
    _updating = false;
    _progress = 100;

    delay(500);
    ESP.restart();

    // Nunca llega aquí
    return OTAResult::SUCCESS;
}

void OTAUpdater::onProgress(OTAProgressCallback cb) {
    _progressCb = cb;
}

bool OTAUpdater::isUpdating() const {
    return _updating;
}

uint8_t OTAUpdater::getProgress() const {
    return _progress;
}

String OTAUpdater::getLastError() const {
    return _lastError;
}
