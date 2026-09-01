/**
 * @file MqttHandler.h
 * @brief Cliente MQTT - Modelo Suscriptor/Visor
 *
 * El dispositivo es un VISOR: se suscribe a tópicos donde un sistema
 * externo (PLC, SCADA, gateway) publica el estado de la máquina.
 * Al recibir datos, actualiza la estructura PDIData compartida que
 * el display y el LED usan para renderizar.
 *
 * Solo publica: su propio status online/offline y audio capturado.
 */

#ifndef MQTT_HANDLER_H
#define MQTT_HANDLER_H

#include <Arduino.h>
#include <WiFi.h>
#include <PubSubClient.h>
#include <ArduinoJson.h>
#include "PDIData.h"
#include "ConfigManager.h"

/** Callback cuando se recibe un dato PDI actualizado */
using MqttPDICallback = std::function<void(const PDIData& data)>;

/** Callback para comandos de control (UPDATE, RESTART, etc.) */
using MqttCommandCallback = std::function<void(const String& command, const String& payload)>;

class MqttHandler {
public:
    MqttHandler(ConfigManager& configMgr);

    /** Inicializa el cliente MQTT con los parámetros de la config */
    void begin();

    /**
     * Actualización periódica: mantiene la conexión y procesa mensajes.
     * Llamar desde loop/tarea de red.
     */
    void update();

    /**
     * Envía un chunk de audio por MQTT (payload binario).
     */
    void publishAudioChunk(const uint8_t* data, size_t length,
                           uint16_t chunkIdx, uint16_t totalChunks);

    /** Registra callback para cuando se reciben datos PDI */
    void onPDIReceived(MqttPDICallback cb);

    /** Registra callback para comandos de control */
    void onCommand(MqttCommandCallback cb);

    /** Indica si está conectado al broker */
    bool isConnected();

    /** Desconecta del broker */
    void disconnect();

    /** Último PDI recibido (acceso directo desde el display) */
    const PDIData& getLastPDI() const;

private:
    ConfigManager& _configMgr;
    WiFiClient _wifiClient;
    PubSubClient _mqttClient;
    MqttPDICallback _pdiCb;
    MqttCommandCallback _commandCb;

    PDIData _receivedPDI;            // Último PDI recibido del broker
    uint32_t _lastReconnectMs;
    uint32_t _lastPdiReceivedMs;     // Timestamp del último dato recibido
    bool _hasReceivedData;           // Se ha recibido al menos un PDI

    static constexpr uint32_t RECONNECT_INTERVAL = 5000;
    static constexpr uint16_t MQTT_BUFFER_SIZE   = 32768;

    /** Intenta reconectar al broker y suscribirse */
    void reconnect();

    /** Callback estática para PubSubClient */
    static void mqttCallbackStatic(char* topic, byte* payload, unsigned int length);
    static MqttHandler* _instance;

    /** Procesa mensaje recibido (dispatcher) */
    void handleMessage(const String& topic, const uint8_t* payload, unsigned int length);

    /** Parsea un JSON de PDI y actualiza _receivedPDI */
    void parsePDI(const uint8_t* payload, unsigned int length);

    // ─── Tópicos MQTT ───
    // Tópicos a los que se SUSCRIBE (datos de la máquina)
    String topicBase() const;
    String topicPDI() const;         // Recibe estado completo
    String topicControl() const;     // Recibe comandos (UPDATE, MODE, etc.)

    // Tópicos donde PUBLICA (solo estado propio y audio)
    String topicDeviceStatus() const;
    String topicAudio() const;
    String topicAudioMeta() const;
};

#endif // MQTT_HANDLER_H
