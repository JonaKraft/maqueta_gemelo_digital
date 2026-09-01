/**
 * @file MqttHandler.cpp
 * @brief Implementación del cliente MQTT - Modelo Suscriptor/Visor
 *
 * Flujo principal:
 *   Sistema externo publica en .../pdi → ESP32 recibe → actualiza PDIData → Display+LED
 */

#include "MqttHandler.h"

MqttHandler* MqttHandler::_instance = nullptr;

MqttHandler::MqttHandler(ConfigManager& configMgr)
    : _configMgr(configMgr)
    , _mqttClient(_wifiClient)
    , _pdiCb(nullptr)
    , _commandCb(nullptr)
    , _lastReconnectMs(0)
    , _lastPdiReceivedMs(0)
    , _hasReceivedData(false)
{
    _instance = this;
    memset(&_receivedPDI, 0, sizeof(_receivedPDI));
}

void MqttHandler::begin() {
    const DeviceConfig& cfg = _configMgr.config();

    _mqttClient.setServer(cfg.mqttHost.c_str(), cfg.mqttPort);
    _mqttClient.setBufferSize(MQTT_BUFFER_SIZE);
    _mqttClient.setCallback(mqttCallbackStatic);
    _mqttClient.setKeepAlive(60);

    Serial.printf("[MQTT] Configurado: %s:%d (buffer=%d)\n",
                  cfg.mqttHost.c_str(), cfg.mqttPort, MQTT_BUFFER_SIZE);
}

void MqttHandler::update() {
    if (!_mqttClient.connected()) {
        uint32_t now = millis();
        if (now - _lastReconnectMs > RECONNECT_INTERVAL) {
            _lastReconnectMs = now;
            reconnect();
        }
    } else {
        _mqttClient.loop();
    }
}

void MqttHandler::reconnect() {
    const DeviceConfig& cfg = _configMgr.config();
    String clientId = cfg.equipmentName + "-" + String(random(0xFFFF), HEX);

    Serial.printf("[MQTT] Conectando como '%s'...\n", clientId.c_str());

    // LWT (Last Will and Testament) para detectar desconexiones
    String willTopic = topicDeviceStatus();
    String willMsg = "{\"online\":false}";

    bool connected;
    if (cfg.mqttUser.length() > 0) {
        connected = _mqttClient.connect(
            clientId.c_str(),
            cfg.mqttUser.c_str(),
            cfg.mqttPassword.c_str(),
            willTopic.c_str(), 1, true, willMsg.c_str()
        );
    } else {
        connected = _mqttClient.connect(
            clientId.c_str(),
            willTopic.c_str(), 1, true, willMsg.c_str()
        );
    }

    if (connected) {
        Serial.println("[MQTT] Conectado al broker!");

        // Publicar que este visor está online
        _mqttClient.publish(topicDeviceStatus().c_str(),
                            "{\"online\":true}", true);

        // ─── SUSCRIPCIONES (lo importante: RECIBIR datos) ───

        // 1. Datos PDI de la máquina (tópico principal)
        _mqttClient.subscribe(topicPDI().c_str());
        Serial.printf("[MQTT] Suscrito a PDI: %s\n", topicPDI().c_str());

        // 2. Comandos de control (UPDATE, RESTART, MODE)
        _mqttClient.subscribe(topicControl().c_str());
        Serial.printf("[MQTT] Suscrito a control: %s\n", topicControl().c_str());

    } else {
        Serial.printf("[MQTT] Fallo conexion, rc=%d\n", _mqttClient.state());
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// Recepción y parseo de mensajes
// ─────────────────────────────────────────────────────────────────────────────

void MqttHandler::mqttCallbackStatic(char* topic, byte* payload, unsigned int length) {
    if (_instance) {
        _instance->handleMessage(String(topic), payload, length);
    }
}

void MqttHandler::handleMessage(const String& topic, const uint8_t* payload,
                                 unsigned int length) {
    Serial.printf("[MQTT] << '%s' (%d bytes)\n", topic.c_str(), length);

    // ─── Datos PDI de la máquina ───
    if (topic == topicPDI()) {
        parsePDI(payload, length);
        return;
    }

    // ─── Comandos de control ───
    if (topic == topicControl()) {
        String message;
        message.reserve(length);
        for (unsigned int i = 0; i < length; i++) {
            message += (char)payload[i];
        }

        JsonDocument doc;
        DeserializationError err = deserializeJson(doc, message);

        String command;
        String cmdPayload;

        if (!err && doc["cmd"].is<const char*>()) {
            command = doc["cmd"].as<String>();
            if (doc["payload"].is<const char*>()) {
                cmdPayload = doc["payload"].as<String>();
            }
        } else {
            command = message;
            command.trim();
            command.toUpperCase();
        }

        Serial.printf("[MQTT] Comando: '%s'\n", command.c_str());

        if (_commandCb) {
            _commandCb(command, cmdPayload);
        }
    }
}

void MqttHandler::parsePDI(const uint8_t* payload, unsigned int length) {
    JsonDocument doc;
    DeserializationError err = deserializeJson(doc, payload, length);

    if (err) {
        Serial.printf("[MQTT] Error parseando PDI: %s\n", err.c_str());
        return;
    }

    // ─── Mapear JSON → estructura PDIData ───

    if (doc["EquipmentName"].is<const char*>())
        _receivedPDI.equipmentName = doc["EquipmentName"].as<String>();
    if (doc["AssetId"].is<const char*>())
        _receivedPDI.assetId = doc["AssetId"].as<String>();
    if (doc["Timestamp"].is<uint32_t>())
        _receivedPDI.timestamp = doc["Timestamp"];

    // Status
    JsonObject status = doc["Status"];
    if (!status.isNull()) {
        if (status["StateCurrent"].is<const char*>())
            _receivedPDI.status.stateCurrent = strToMachineState(status["StateCurrent"]);
        if (status["UnitModeCurrent"].is<const char*>()) {
            String mode = status["UnitModeCurrent"].as<String>();
            if (mode == "Auto")        _receivedPDI.status.unitModeCurrent = UnitMode::AUTO;
            else if (mode == "Manual") _receivedPDI.status.unitModeCurrent = UnitMode::MANUAL;
            else if (mode == "Setup")  _receivedPDI.status.unitModeCurrent = UnitMode::SETUP;
        }
        if (status["MachSpeed"].is<float>())
            _receivedPDI.status.machSpeed = status["MachSpeed"];
        if (status["CurMachSpeed"].is<float>())
            _receivedPDI.status.curMachSpeed = status["CurMachSpeed"];
        if (status["StatePrevious"].is<const char*>())
            _receivedPDI.status.statePrevious = strToMachineState(status["StatePrevious"]);
        if (status["StateDuration"].is<uint32_t>())
            _receivedPDI.status.stateDuration = status["StateDuration"];
    }

    // Admin
    JsonObject admin = doc["Admin"];
    if (!admin.isNull()) {
        if (admin["ProdProcessedCount"]["Count"].is<uint32_t>())
            _receivedPDI.admin.prodProcessedCount = admin["ProdProcessedCount"]["Count"];
        if (admin["ProdDefectiveCount"]["Count"].is<uint32_t>())
            _receivedPDI.admin.prodDefectiveCount = admin["ProdDefectiveCount"]["Count"];
    }

    // Calidad
    if (doc["QualitySignal"].is<const char*>()) {
        String q = doc["QualitySignal"].as<String>();
        if (q == "OK")             _receivedPDI.qualitySignal = QualitySignal::OK;
        else if (q == "Bad")       _receivedPDI.qualitySignal = QualitySignal::BAD;
        else if (q == "Uncertain") _receivedPDI.qualitySignal = QualitySignal::UNCERTAIN;
    }

    // Contexto extendido
    JsonObject ctx = doc["Context"];
    if (!ctx.isNull()) {
        if (ctx["OrderNo"].is<const char*>())     _receivedPDI.context.orderNo = ctx["OrderNo"].as<String>();
        if (ctx["ProductCode"].is<const char*>())  _receivedPDI.context.productCode = ctx["ProductCode"].as<String>();
        if (ctx["RecipeId"].is<const char*>())     _receivedPDI.context.recipeId = ctx["RecipeId"].as<String>();
        if (ctx["LotBatch"].is<const char*>())     _receivedPDI.context.lotBatch = ctx["LotBatch"].as<String>();
        if (ctx["LineId"].is<const char*>())       _receivedPDI.context.lineId = ctx["LineId"].as<String>();
    }

    // Alarmas
    JsonObject alm = doc["Alarms"];
    if (!alm.isNull()) {
        _receivedPDI.alarms.alarmActive    = alm["AlarmActive"] | false;
        if (alm["AlarmCode"].is<const char*>())     _receivedPDI.alarms.alarmCode = alm["AlarmCode"].as<String>();
        if (alm["AlarmText"].is<const char*>())     _receivedPDI.alarms.alarmText = alm["AlarmText"].as<String>();
        if (alm["StopReasonCode"].is<const char*>()) _receivedPDI.alarms.stopReasonCode = alm["StopReasonCode"].as<String>();
        if (alm["StopReasonDesc"].is<const char*>()) _receivedPDI.alarms.stopReasonDesc = alm["StopReasonDesc"].as<String>();
    }

    // Mediciones
    JsonObject meas = doc["Measurements"];
    if (!meas.isNull()) {
        if (meas["Power"].is<float>())       _receivedPDI.measurements.power = meas["Power"];
        if (meas["Current"].is<float>())     _receivedPDI.measurements.current = meas["Current"];
        if (meas["AirPressure"].is<float>()) _receivedPDI.measurements.airPressure = meas["AirPressure"];
    }

    _receivedPDI.connectionStatus = true;
    _receivedPDI.deviceMode = DeviceMode::ONLINE;
    _lastPdiReceivedMs = millis();
    _hasReceivedData = true;

    Serial.printf("[MQTT] PDI recibido: %s | %s | Vel=%.1f\n",
                  _receivedPDI.equipmentName.c_str(),
                  machineStateToStr(_receivedPDI.status.stateCurrent),
                  _receivedPDI.status.curMachSpeed);

    // Notificar al callback
    if (_pdiCb) {
        _pdiCb(_receivedPDI);
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// Audio por MQTT (fragmentado) - el único dato que PUBLICA
// ─────────────────────────────────────────────────────────────────────────────

void MqttHandler::publishAudioChunk(const uint8_t* data, size_t length,
                                     uint16_t chunkIdx, uint16_t totalChunks) {
    if (!_mqttClient.connected()) return;

    String metaTopic = topicAudioMeta();
    JsonDocument meta;
    meta["chunk"]  = chunkIdx;
    meta["total"]  = totalChunks;
    meta["size"]   = length;
    meta["ts"]     = millis();

    char metaBuf[128];
    serializeJson(meta, metaBuf, sizeof(metaBuf));
    _mqttClient.publish(metaTopic.c_str(), metaBuf);

    String chunkTopic = topicAudio() + "/" + String(chunkIdx);
    _mqttClient.publish(chunkTopic.c_str(), data, length);

    Serial.printf("[MQTT] Audio chunk %d/%d enviado (%d bytes)\n",
                  chunkIdx + 1, totalChunks, length);
}

// ─────────────────────────────────────────────────────────────────────────────
// Callbacks y accesores
// ─────────────────────────────────────────────────────────────────────────────

void MqttHandler::onPDIReceived(MqttPDICallback cb) {
    _pdiCb = cb;
}

void MqttHandler::onCommand(MqttCommandCallback cb) {
    _commandCb = cb;
}

bool MqttHandler::isConnected() {
    return _mqttClient.connected();
}

const PDIData& MqttHandler::getLastPDI() const {
    return _receivedPDI;
}

void MqttHandler::disconnect() {
    if (_mqttClient.connected()) {
        _mqttClient.publish(topicDeviceStatus().c_str(),
                            "{\"online\":false}", true);
        _mqttClient.disconnect();
    }
    Serial.println("[MQTT] Desconectado del broker");
}

// ─────────────────────────────────────────────────────────────────────────────
// Generación de tópicos
// ─────────────────────────────────────────────────────────────────────────────

String MqttHandler::topicBase() const {
    return "colmena/gemelo/" + _configMgr.config().equipmentName;
}

String MqttHandler::topicPDI() const {
    return topicBase() + "/pdi";
}

String MqttHandler::topicControl() const {
    return topicBase() + "/control";
}

String MqttHandler::topicDeviceStatus() const {
    return topicBase() + "/device/status";
}

String MqttHandler::topicAudio() const {
    return topicBase() + "/audio/data";
}

String MqttHandler::topicAudioMeta() const {
    return topicBase() + "/audio/meta";
}
