/**
 * @file WifiPortal.cpp
 * @brief Implementación del gestor WiFi con portal cautivo
 */

#include "WifiPortal.h"

WifiPortal::WifiPortal(ConfigManager& configMgr, const char* apSSID)
    : _configMgr(configMgr)
    , _apSSID(apSSID)
    , _state(WifiState::DISCONNECTED)
    , _connectStartMs(0)
    , _lastReconnectMs(0)
    , _server(nullptr)
    , _dns(nullptr)
{}

void WifiPortal::begin() {
    WiFi.mode(WIFI_STA);
    WiFi.setAutoReconnect(true);

    const String& ssid = _configMgr.config().wifiSSID;
    const String& pass = _configMgr.config().wifiPassword;

    if (ssid.length() > 0) {
        Serial.printf("[WiFi] Intentando conectar a '%s'...\n", ssid.c_str());
        WiFi.begin(ssid.c_str(), pass.c_str());
        _state = WifiState::CONNECTING;
        _connectStartMs = millis();
    } else {
        Serial.println("[WiFi] Sin SSID configurado, iniciando portal");
        startPortal();
    }
}

void WifiPortal::update() {
    switch (_state) {
        case WifiState::CONNECTING: {
            if (WiFi.status() == WL_CONNECTED) {
                _state = WifiState::CONNECTED;
                Serial.printf("[WiFi] Conectado! IP: %s\n", WiFi.localIP().toString().c_str());
            } else if (millis() - _connectStartMs > CONNECT_TIMEOUT_MS) {
                Serial.println("[WiFi] Timeout de conexion, iniciando portal");
                startPortal();
            }
            break;
        }

        case WifiState::CONNECTED: {
            if (WiFi.status() != WL_CONNECTED) {
                Serial.println("[WiFi] Conexion perdida, reintentando...");
                _state = WifiState::CONNECTING;
                _connectStartMs = millis();
                WiFi.reconnect();
            }
            break;
        }

        case WifiState::AP_ACTIVE: {
            // Atender peticiones DNS y HTTP del portal
            if (_dns) _dns->processNextRequest();
            if (_server) _server->handleClient();
            break;
        }

        case WifiState::AP_CONFIGURED: {
            // El usuario envió datos: detener portal e intentar conectar
            stopPortal();
            begin();
            break;
        }

        case WifiState::DISCONNECTED:
            // Nada que hacer en modo OFF
            break;
    }
}

void WifiPortal::startPortal() {
    // Detener cualquier conexión STA previa
    WiFi.disconnect(true);
    delay(100);

    // Configurar modo AP+STA para permitir escaneo
    WiFi.mode(WIFI_AP);
    WiFi.softAP(_apSSID);
    delay(100);

    Serial.printf("[WiFi] AP activo: '%s' IP: %s\n",
                  _apSSID, WiFi.softAPIP().toString().c_str());

    // Servidor DNS cautivo: redirige todo al AP
    if (!_dns) _dns = new DNSServer();
    _dns->start(53, "*", WiFi.softAPIP());

    // Servidor web
    if (!_server) _server = new WebServer(80);
    setupWebServer();
    _server->begin();

    _state = WifiState::AP_ACTIVE;
    Serial.println("[WiFi] Portal cautivo iniciado");
}

void WifiPortal::stopPortal() {
    if (_server) {
        _server->stop();
        delete _server;
        _server = nullptr;
    }
    if (_dns) {
        _dns->stop();
        delete _dns;
        _dns = nullptr;
    }
    WiFi.softAPdisconnect(true);
    Serial.println("[WiFi] Portal cautivo detenido");
}

void WifiPortal::disconnect() {
    stopPortal();
    WiFi.disconnect(true);
    WiFi.mode(WIFI_OFF);
    _state = WifiState::DISCONNECTED;
    Serial.println("[WiFi] Desconectado completamente");
}

WifiState WifiPortal::getState() const {
    return _state;
}

String WifiPortal::getIP() const {
    if (_state == WifiState::CONNECTED) {
        return WiFi.localIP().toString();
    }
    if (_state == WifiState::AP_ACTIVE) {
        return WiFi.softAPIP().toString();
    }
    return "0.0.0.0";
}

int WifiPortal::getRSSI() const {
    if (_state == WifiState::CONNECTED) {
        return WiFi.RSSI();
    }
    return 0;
}

bool WifiPortal::isPortalActive() const {
    return _state == WifiState::AP_ACTIVE;
}

// ─────────────────────────────────────────────────────────────────────────────
// Servidor Web del Portal Cautivo
// ─────────────────────────────────────────────────────────────────────────────

void WifiPortal::setupWebServer() {
    _server->on("/", HTTP_GET, [this]() { handleRoot(); });
    _server->on("/save", HTTP_POST, [this]() { handleSave(); });
    _server->on("/status", HTTP_GET, [this]() { handleStatus(); });
    _server->onNotFound([this]() { handleNotFound(); });
}

void WifiPortal::handleRoot() {
    _server->send(200, "text/html", buildConfigPage());
}

void WifiPortal::handleSave() {
    // Leer parámetros del formulario
    if (_server->hasArg("ssid")) {
        _configMgr.setWifiCredentials(
            _server->arg("ssid"),
            _server->arg("password")
        );
    }
    if (_server->hasArg("mqtt_host")) {
        _configMgr.setMqttParams(
            _server->arg("mqtt_host"),
            _server->arg("mqtt_port").toInt()
        );
    }
    if (_server->hasArg("ota_url")) {
        _configMgr.setOtaParams(
            _server->arg("ota_url"),
            _server->arg("ota_port").toInt()
        );
    }
    if (_server->hasArg("equip_name")) {
        _configMgr.setEquipmentName(_server->arg("equip_name"));
    }
    if (_server->hasArg("dev_mode")) {
        int mode = _server->arg("dev_mode").toInt();
        _configMgr.setDeviceMode(static_cast<DeviceMode>(mode));
    }

    _configMgr.save();

    String html = R"rawhtml(
<!DOCTYPE html><html><head>
<meta charset="UTF-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>Guardado</title>
<style>
body{font-family:Arial,sans-serif;background:#1a1a2e;color:#e0e0e0;
text-align:center;padding:40px 20px;}
.ok{background:#16a34a;color:white;padding:20px;border-radius:12px;
font-size:1.2em;max-width:400px;margin:0 auto;}
</style></head><body>
<div class="ok">Configuracion guardada.<br>El dispositivo se reiniciara...</div>
</body></html>
)rawhtml";

    _server->send(200, "text/html", html);

    // Marcar como configurado para que update() inicie reconexión
    _state = WifiState::AP_CONFIGURED;
}

void WifiPortal::handleStatus() {
    String json = "{\"state\":\"" + String(static_cast<int>(_state)) + "\","
                  "\"ip\":\"" + getIP() + "\","
                  "\"rssi\":" + String(getRSSI()) + "}";
    _server->send(200, "application/json", json);
}

void WifiPortal::handleNotFound() {
    // Redirigir al portal cautivo
    _server->sendHeader("Location", "http://192.168.4.1/", true);
    _server->send(302, "text/plain", "");
}

String WifiPortal::buildConfigPage() {
    const DeviceConfig& cfg = _configMgr.config();

    String html = R"rawhtml(
<!DOCTYPE html><html><head>
<meta charset="UTF-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>Gemelo Digital - Configuracion</title>
<style>
*{box-sizing:border-box;margin:0;padding:0;}
body{font-family:'Segoe UI',Arial,sans-serif;background:#0f0f23;color:#e0e0e0;
min-height:100vh;}
.header{background:linear-gradient(135deg,#1e3a5f,#2d5a87);padding:20px;
text-align:center;border-bottom:3px solid #4a9eff;}
.header h1{font-size:1.3em;color:#4a9eff;}
.header p{font-size:0.85em;color:#8ab4f8;margin-top:5px;}
.container{max-width:480px;margin:0 auto;padding:15px;}
.section{background:#1a1a2e;border-radius:12px;padding:18px;margin:12px 0;
border:1px solid #2a2a4a;}
.section h2{color:#4a9eff;font-size:1em;margin-bottom:12px;
border-bottom:1px solid #2a2a4a;padding-bottom:8px;}
label{display:block;font-size:0.85em;color:#8ab4f8;margin:8px 0 4px;}
input[type="text"],input[type="password"],input[type="number"],select{
width:100%;padding:10px;border:1px solid #3a3a5a;border-radius:8px;
background:#0f0f23;color:#e0e0e0;font-size:0.95em;}
input:focus,select:focus{outline:none;border-color:#4a9eff;
box-shadow:0 0 5px rgba(74,158,255,0.3);}
.btn{display:block;width:100%;padding:14px;margin:20px 0 10px;border:none;
border-radius:10px;font-size:1.05em;font-weight:bold;cursor:pointer;
background:linear-gradient(135deg,#1e7a34,#2da44e);color:white;
transition:transform 0.1s;}
.btn:active{transform:scale(0.98);}
.mode-sel{display:flex;gap:8px;margin-top:6px;}
.mode-sel label{flex:1;text-align:center;padding:10px 5px;
border-radius:8px;border:2px solid #3a3a5a;cursor:pointer;font-size:0.85em;}
.mode-sel input{display:none;}
.mode-sel input:checked+span{color:#4a9eff;}
.mode-sel label:has(input:checked){border-color:#4a9eff;background:#1a2a4a;}
</style></head><body>
<div class="header">
<h1>GEMELO DIGITAL</h1>
<p>Portal de Configuracion</p>
</div>
<div class="container">
<form action="/save" method="POST">

<div class="section">
<h2>WiFi</h2>
<label>SSID</label>
<input type="text" name="ssid" value=")rawhtml";
    html += cfg.wifiSSID;
    html += R"rawhtml(" placeholder="Nombre de la red WiFi" required>
<label>Password</label>
<input type="password" name="password" value=")rawhtml";
    html += cfg.wifiPassword;
    html += R"rawhtml(" placeholder="Contrasena WiFi">
</div>

<div class="section">
<h2>MQTT (Broker Colmena)</h2>
<label>Host / IP</label>
<input type="text" name="mqtt_host" value=")rawhtml";
    html += cfg.mqttHost;
    html += R"rawhtml(" placeholder="broker.colmena.io">
<label>Puerto</label>
<input type="number" name="mqtt_port" value=")rawhtml";
    html += String(cfg.mqttPort);
    html += R"rawhtml(" placeholder="1883">
</div>

<div class="section">
<h2>OTA - Actualizacion Remota</h2>
<label>URL del Servidor</label>
<input type="text" name="ota_url" value=")rawhtml";
    html += cfg.otaUrl;
    html += R"rawhtml(" placeholder="http://192.168.1.100">
<label>Puerto</label>
<input type="number" name="ota_port" value=")rawhtml";
    html += String(cfg.otaPort);
    html += R"rawhtml(" placeholder="8080">
</div>

<div class="section">
<h2>Identificacion</h2>
<label>Nombre del Equipo</label>
<input type="text" name="equip_name" value=")rawhtml";
    html += cfg.equipmentName;
    html += R"rawhtml(" placeholder="ESP32-GD-001">
</div>

<div class="section">
<h2>Modo de Operacion</h2>
<div class="mode-sel">
<label><input type="radio" name="dev_mode" value="0")rawhtml";
    if (cfg.deviceMode == DeviceMode::OFF) html += " checked";
    html += R"rawhtml(><span>OFF</span></label>
<label><input type="radio" name="dev_mode" value="1")rawhtml";
    if (cfg.deviceMode == DeviceMode::ONLINE) html += " checked";
    html += R"rawhtml(><span>ONLINE</span></label>
<label><input type="radio" name="dev_mode" value="2")rawhtml";
    if (cfg.deviceMode == DeviceMode::SIMULATOR) html += " checked";
    html += R"rawhtml(><span>SIMULADOR</span></label>
</div>
</div>

<button type="submit" class="btn">GUARDAR Y REINICIAR</button>
</form>
</div>
</body></html>
)rawhtml";

    return html;
}
