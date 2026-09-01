/**
 * @file ConfigManager.h
 * @brief Gestión de configuración persistente en NVS (Preferences)
 *
 * Almacena y recupera parámetros de WiFi, MQTT, OTA y modo de operación
 * en la memoria no volátil del ESP32-S3.
 */

#ifndef CONFIG_MANAGER_H
#define CONFIG_MANAGER_H

#include <Arduino.h>
#include <Preferences.h>
#include "PDIData.h"

/** Estructura con todos los parámetros configurables */
struct DeviceConfig {
    // --- WiFi ---
    String wifiSSID;
    String wifiPassword;

    // --- MQTT ---
    String mqttHost;
    uint16_t mqttPort;
    String mqttUser;        // Futuro: autenticación
    String mqttPassword;    // Futuro: autenticación
    String equipmentName;   // Nombre del equipo (usado como clientId y en tópicos)
    String assetId;

    // --- OTA ---
    String otaUrl;          // URL base del servidor de firmware (ej: http://192.168.1.100)
    uint16_t otaPort;       // Puerto del servidor HTTP de firmware

    // --- Modo de operación ---
    DeviceMode deviceMode;

    // --- Audio ---
    uint8_t volume = 80;  // 0-100, default 80%
};

class ConfigManager {
public:
    ConfigManager();

    /** Inicializa el namespace de Preferences y carga la config */
    void begin();

    /** Carga todos los valores desde NVS. Si no existen, usa defaults */
    void load();

    /** Persiste todos los valores actuales en NVS */
    void save();

    /** Resetea la configuración a valores de fábrica */
    void resetToDefaults();

    /** Acceso directo a la configuración actual */
    DeviceConfig& config();
    const DeviceConfig& config() const;

    // --- Setters individuales con persistencia inmediata ---
    void setWifiCredentials(const String& ssid, const String& password);
    void setMqttParams(const String& host, uint16_t port);
    void setOtaParams(const String& url, uint16_t port);
    void setDeviceMode(DeviceMode mode);
    void setEquipmentName(const String& name);
    void setVolume(uint8_t vol);

private:
    Preferences _prefs;
    DeviceConfig _config;

    static constexpr const char* NVS_NAMESPACE = "gemelo_cfg";

    // Claves NVS
    static constexpr const char* KEY_WIFI_SSID   = "wifi_ssid";
    static constexpr const char* KEY_WIFI_PASS   = "wifi_pass";
    static constexpr const char* KEY_MQTT_HOST   = "mqtt_host";
    static constexpr const char* KEY_MQTT_PORT   = "mqtt_port";
    static constexpr const char* KEY_MQTT_USER   = "mqtt_user";
    static constexpr const char* KEY_MQTT_PASSWD = "mqtt_passwd";
    static constexpr const char* KEY_OTA_URL     = "ota_url";
    static constexpr const char* KEY_OTA_PORT    = "ota_port";
    static constexpr const char* KEY_DEV_MODE    = "dev_mode";
    static constexpr const char* KEY_EQUIP_NAME  = "equip_name";
    static constexpr const char* KEY_ASSET_ID    = "asset_id";
    static constexpr const char* KEY_VOLUME      = "volume";
};

#endif // CONFIG_MANAGER_H
