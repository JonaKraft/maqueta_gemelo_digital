/**
 * @file OTAUpdater.h
 * @brief Actualización remota de firmware por HTTP (OTA)
 *
 * Al recibir el comando "UPDATE" por MQTT, descarga el binario
 * del servidor configurado y lo aplica usando la API de Update del ESP32.
 * Soporta esquema de particiones dual (OTA_0/OTA_1).
 */

#ifndef OTA_UPDATER_H
#define OTA_UPDATER_H

#include <Arduino.h>
#include <HTTPClient.h>
#include <Update.h>
#include "ConfigManager.h"

/** Callback para reportar progreso de la actualización */
using OTAProgressCallback = std::function<void(size_t current, size_t total)>;

/** Resultado de la operación OTA */
enum class OTAResult : uint8_t {
    SUCCESS       = 0,
    HTTP_ERROR    = 1,
    WRITE_ERROR   = 2,
    VERIFY_ERROR  = 3,
    NO_UPDATES    = 4,
    IN_PROGRESS   = 5
};

class OTAUpdater {
public:
    OTAUpdater(ConfigManager& configMgr);

    /**
     * Inicia la descarga y aplicación del firmware.
     * @param firmwarePath  Ruta del binario en el servidor (ej: "/firmware.bin")
     * @return              Resultado de la operación
     */
    OTAResult startUpdate(const String& firmwarePath = "/firmware.bin");

    /** Registra callback de progreso */
    void onProgress(OTAProgressCallback cb);

    /** Indica si hay una actualización en curso */
    bool isUpdating() const;

    /** Obtiene el porcentaje de progreso actual */
    uint8_t getProgress() const;

    /** Obtiene el último error como texto */
    String getLastError() const;

private:
    ConfigManager& _configMgr;
    OTAProgressCallback _progressCb;
    bool _updating;
    uint8_t _progress;
    String _lastError;
};

#endif // OTA_UPDATER_H
