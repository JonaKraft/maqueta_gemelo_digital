/**
 * @file WifiPortal.h
 * @brief Gestión WiFi no bloqueante con Portal Cautivo de configuración
 *
 * Si el ESP32-S3 no se conecta al WiFi guardado en 30 segundos,
 * o si el usuario lo fuerza desde el menú, arranca un AP con un
 * servidor web (portal cautivo) para configurar WiFi, MQTT y OTA.
 */

#ifndef WIFI_PORTAL_H
#define WIFI_PORTAL_H

#include <Arduino.h>
#include <WiFi.h>
#include <WebServer.h>
#include <DNSServer.h>
#include "ConfigManager.h"

/** Estados del módulo WiFi */
enum class WifiState : uint8_t {
    DISCONNECTED   = 0,
    CONNECTING     = 1,
    CONNECTED      = 2,
    AP_ACTIVE      = 3,   // Portal cautivo activo
    AP_CONFIGURED  = 4    // El usuario envió datos desde el portal
};

class WifiPortal {
public:
    /**
     * @param configMgr  Referencia al gestor de configuración
     * @param apSSID     Nombre del AP para el portal cautivo
     */
    WifiPortal(ConfigManager& configMgr, const char* apSSID = "GemeloDigital-Setup");

    /** Inicia el intento de conexión WiFi (no bloqueante) */
    void begin();

    /**
     * Actualización periódica (llamar desde loop/tarea).
     * Gestiona timeouts, reconexiones y el servidor web del portal.
     */
    void update();

    /** Fuerza el inicio del portal cautivo (desde menú) */
    void startPortal();

    /** Detiene el AP y el portal */
    void stopPortal();

    /** Desconecta completamente WiFi (para modo OFF) */
    void disconnect();

    /** Estado actual del módulo WiFi */
    WifiState getState() const;

    /** IP local cuando está conectado, o IP del AP */
    String getIP() const;

    /** RSSI de la conexión actual */
    int getRSSI() const;

    /** Indica si el portal cautivo está activo */
    bool isPortalActive() const;

private:
    ConfigManager& _configMgr;
    const char* _apSSID;

    WifiState _state;
    uint32_t _connectStartMs;
    uint32_t _lastReconnectMs;

    WebServer* _server;
    DNSServer* _dns;

    static constexpr uint32_t CONNECT_TIMEOUT_MS  = 30000;  // 30 seg
    static constexpr uint32_t RECONNECT_INTERVAL  = 15000;  // 15 seg entre reintentos

    // Handlers del servidor web
    void setupWebServer();
    void handleRoot();
    void handleSave();
    void handleStatus();
    void handleNotFound();

    /** Genera el HTML del portal de configuración */
    String buildConfigPage();
};

#endif // WIFI_PORTAL_H
