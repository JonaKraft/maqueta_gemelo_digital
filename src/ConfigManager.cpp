/**
 * @file ConfigManager.cpp
 * @brief Implementación de la gestión de configuración persistente
 */

#include "ConfigManager.h"

ConfigManager::ConfigManager() {}

void ConfigManager::begin() {
    _prefs.begin(NVS_NAMESPACE, false); // false = read/write
    load();
}

void ConfigManager::load() {
    _config.wifiSSID      = _prefs.getString(KEY_WIFI_SSID, "");
    _config.wifiPassword  = _prefs.getString(KEY_WIFI_PASS, "");
    _config.mqttHost      = _prefs.getString(KEY_MQTT_HOST, "broker.colmena.io");
    _config.mqttPort      = _prefs.getUShort(KEY_MQTT_PORT, 1883);
    _config.mqttUser      = _prefs.getString(KEY_MQTT_USER, "");
    _config.mqttPassword  = _prefs.getString(KEY_MQTT_PASSWD, "");
    _config.otaUrl        = _prefs.getString(KEY_OTA_URL, "http://192.168.1.100");
    _config.otaPort       = _prefs.getUShort(KEY_OTA_PORT, 8080);
    _config.equipmentName = _prefs.getString(KEY_EQUIP_NAME, "ESP32-GD-001");
    _config.assetId       = _prefs.getString(KEY_ASSET_ID, "ASSET-001");
    _config.deviceMode    = static_cast<DeviceMode>(
                                _prefs.getUChar(KEY_DEV_MODE,
                                    static_cast<uint8_t>(DeviceMode::ONLINE)));
    _config.volume        = _prefs.getUChar(KEY_VOLUME, 80);

    Serial.println("[ConfigManager] Configuracion cargada desde NVS");
    Serial.printf("  WiFi SSID: %s\n", _config.wifiSSID.c_str());
    Serial.printf("  MQTT Host: %s:%d\n", _config.mqttHost.c_str(), _config.mqttPort);
    Serial.printf("  OTA URL:   %s:%d\n", _config.otaUrl.c_str(), _config.otaPort);
    Serial.printf("  Equipo:    %s\n", _config.equipmentName.c_str());
    Serial.printf("  Modo:      %s\n", deviceModeToStr(_config.deviceMode));
    Serial.printf("  Volumen:   %d%%\n", _config.volume);
}

void ConfigManager::save() {
    _prefs.putString(KEY_WIFI_SSID, _config.wifiSSID);
    _prefs.putString(KEY_WIFI_PASS, _config.wifiPassword);
    _prefs.putString(KEY_MQTT_HOST, _config.mqttHost);
    _prefs.putUShort(KEY_MQTT_PORT, _config.mqttPort);
    _prefs.putString(KEY_MQTT_USER, _config.mqttUser);
    _prefs.putString(KEY_MQTT_PASSWD, _config.mqttPassword);
    _prefs.putString(KEY_OTA_URL, _config.otaUrl);
    _prefs.putUShort(KEY_OTA_PORT, _config.otaPort);
    _prefs.putUChar(KEY_DEV_MODE, static_cast<uint8_t>(_config.deviceMode));
    _prefs.putString(KEY_EQUIP_NAME, _config.equipmentName);
    _prefs.putString(KEY_ASSET_ID, _config.assetId);
    _prefs.putUChar(KEY_VOLUME, _config.volume);

    Serial.println("[ConfigManager] Configuracion guardada en NVS");
}

void ConfigManager::resetToDefaults() {
    _prefs.clear();
    _config.wifiSSID      = "";
    _config.wifiPassword  = "";
    _config.mqttHost      = "broker.colmena.io";
    _config.mqttPort      = 1883;
    _config.mqttUser      = "";
    _config.mqttPassword  = "";
    _config.otaUrl        = "http://192.168.1.100";
    _config.otaPort       = 8080;
    _config.equipmentName = "ESP32-GD-001";
    _config.assetId       = "ASSET-001";
    _config.deviceMode    = DeviceMode::ONLINE;
    _config.volume        = 80;
    save();
    Serial.println("[ConfigManager] Reseteo a valores de fabrica");
}

DeviceConfig& ConfigManager::config() {
    return _config;
}

const DeviceConfig& ConfigManager::config() const {
    return _config;
}

void ConfigManager::setWifiCredentials(const String& ssid, const String& password) {
    _config.wifiSSID = ssid;
    _config.wifiPassword = password;
    _prefs.putString(KEY_WIFI_SSID, ssid);
    _prefs.putString(KEY_WIFI_PASS, password);
}

void ConfigManager::setMqttParams(const String& host, uint16_t port) {
    _config.mqttHost = host;
    _config.mqttPort = port;
    _prefs.putString(KEY_MQTT_HOST, host);
    _prefs.putUShort(KEY_MQTT_PORT, port);
}

void ConfigManager::setOtaParams(const String& url, uint16_t port) {
    _config.otaUrl = url;
    _config.otaPort = port;
    _prefs.putString(KEY_OTA_URL, url);
    _prefs.putUShort(KEY_OTA_PORT, port);
}

void ConfigManager::setDeviceMode(DeviceMode mode) {
    _config.deviceMode = mode;
    _prefs.putUChar(KEY_DEV_MODE, static_cast<uint8_t>(mode));
}

void ConfigManager::setEquipmentName(const String& name) {
    _config.equipmentName = name;
    _prefs.putString(KEY_EQUIP_NAME, name);
}

void ConfigManager::setVolume(uint8_t vol) {
    _config.volume = vol > 100 ? 100 : vol;
    _prefs.putUChar(KEY_VOLUME, _config.volume);
}
