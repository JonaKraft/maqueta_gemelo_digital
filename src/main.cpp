/**
 * @file main.cpp
 * @brief Punto de entrada del firmware - Gemelo Digital Industrial ESP32-S3
 *
 * Orquesta todos los módulos del sistema usando FreeRTOS:
 *   - Tarea UI (Core 1): Display OLED + Encoder + LED RGB
 *   - Tarea Red (Core 0): WiFi + MQTT + OTA
 *   - Loop principal: Audio PTT + coordinación de modos
 *
 * Arquitectura no bloqueante: ninguna tarea bloquea a las otras.
 */

#include <Arduino.h>
#include "PDIData.h"
#include "ConfigManager.h"
#include "EncoderInput.h"
#include "LedController.h"
#include "WifiPortal.h"
#include "MqttHandler.h"
#include "OTAUpdater.h"
#include "AudioHandler.h"
#include "Simulator.h"
#include "DisplayMenu.h"

// =============================================================================
// CONFIGURACIÓN DE PINES (adaptar a tu PCB)
// =============================================================================

// ─── ESP32-S3-DevKitC-1 N16R8 ───
// GPIOs disponibles: 0-21, 38-48 (33-37 usados por PSRAM octal)
// GPIO 19/20: USB nativo (CDC), GPIO 43/44: UART0

// Encoder rotativo
static constexpr uint8_t PIN_ENC_CLK = 39;
static constexpr uint8_t PIN_ENC_DT  = 40;
static constexpr uint8_t PIN_ENC_SW  = 41;

// LED RGB NeoPixel (WS2812 onboard del DevKitC-1)
static constexpr uint8_t PIN_LED_NEO = 48;

// I2C para OLED (pines válidos en ESP32-S3)
static constexpr uint8_t PIN_I2C_SDA = 5;
static constexpr uint8_t PIN_I2C_SCL = 6;

// Audio Push-to-Talk (micrófono INMP441)
static constexpr uint8_t PIN_PTT    = 4;    // GPIO0 es BOOT, evitarlo
static constexpr uint8_t PIN_I2S_WS  = 15;
static constexpr uint8_t PIN_I2S_SCK = 16;
static constexpr uint8_t PIN_I2S_SD  = 17;

// Audio Speaker (MAX98357A)
static constexpr uint8_t PIN_SPK_BCLK = 7;
static constexpr uint8_t PIN_SPK_LRC  = 8;
static constexpr uint8_t PIN_SPK_DIN  = 18;
static constexpr uint8_t PIN_AMP_EN   = 10;   // BC337 habilitación amplificador

// =============================================================================
// INSTANCIAS GLOBALES DE MÓDULOS
// =============================================================================

ConfigManager configMgr;
EncoderInput  encoder(PIN_ENC_CLK, PIN_ENC_DT, PIN_ENC_SW);
LedController led(PIN_LED_NEO, 1);
WifiPortal    wifiPortal(configMgr);
MqttHandler   mqtt(configMgr);
OTAUpdater    ota(configMgr);
AudioHandler  audio(mqtt);
Simulator     simulator;
DisplayMenu   display(PIN_I2C_SDA, PIN_I2C_SCL);

// Estructura PDI compartida (protegida por mutex)
PDIData       pdiData;
SemaphoreHandle_t pdiMutex;

// Handles de las tareas FreeRTOS
TaskHandle_t taskUIHandle   = nullptr;
TaskHandle_t taskNetHandle  = nullptr;

// =============================================================================
// PROTOTIPOS DE TAREAS Y CALLBACKS
// =============================================================================

void taskUI(void* parameter);
void taskNetwork(void* parameter);
void onMqttCommand(const String& command, const String& payload);
void onMenuAction(uint8_t actionId);
void applyDeviceMode(DeviceMode newMode);

// =============================================================================
// SETUP
// =============================================================================

void setup() {
    Serial.begin(115200);
    while (!Serial && millis() < 3000); // Esperar USB CDC (máx 3 seg)
    Serial.println("\n====================================");
    Serial.println(" GEMELO DIGITAL - ESP32-S3");
    Serial.println(" Firmware v1.0.0");
    Serial.println("====================================\n");
    Serial.flush();

    // Crear mutex para acceso a pdiData
    pdiMutex = xSemaphoreCreateMutex();

    // --- Inicializar módulos base (con diagnóstico) ---
    Serial.println("[Setup] 1/7 ConfigManager..."); Serial.flush();
    configMgr.begin();

    Serial.println("[Setup] 2/7 Encoder..."); Serial.flush();
    encoder.begin();

    Serial.println("[Setup] 3/7 LED NeoPixel..."); Serial.flush();
    led.begin();

    Serial.println("[Setup] 4/7 Display OLED..."); Serial.flush();
    display.begin();

    // Mostrar splash (no bloquea si el display no está conectado)
    display.showSplash("v1.0.0");
    led.setColor(0, 0, 255);
    led.setPattern(LedPattern::BREATHE);

    // Inicializar datos PDI con valores default
    pdiData.equipmentName = configMgr.config().equipmentName;
    pdiData.assetId = configMgr.config().assetId;
    pdiData.status.stateCurrent = MachineState::IDLE;
    pdiData.status.unitModeCurrent = UnitMode::MANUAL;
    pdiData.deviceMode = configMgr.config().deviceMode;
    pdiData.connectionStatus = false;

    // Registrar callbacks
    mqtt.onCommand(onMqttCommand);
    display.onAction(onMenuAction);

    // Callback cuando el broker envía datos PDI de la máquina
    mqtt.onPDIReceived([](const PDIData& incoming) {
        if (xSemaphoreTake(pdiMutex, pdMS_TO_TICKS(50)) == pdTRUE) {
            pdiData = incoming;
            xSemaphoreGive(pdiMutex);
        }
    });

    // Configurar callback de progreso OTA
    ota.onProgress([](size_t current, size_t total) {
        uint8_t percent = (uint8_t)(current * 100 / total);
        display.showOTAProgress(percent, "Descargando...");
    });

    Serial.println("[Setup] 5/7 Audio I2S..."); Serial.flush();
    AudioConfig audioConfig;
    audioConfig.pttPin    = PIN_PTT;
    audioConfig.i2sWsPin  = PIN_I2S_WS;
    audioConfig.i2sSckPin = PIN_I2S_SCK;
    audioConfig.i2sSdPin  = PIN_I2S_SD;
    audioConfig.i2sSpkWsPin  = PIN_SPK_LRC;
    audioConfig.i2sSpkSckPin = PIN_SPK_BCLK;
    audioConfig.i2sSpkSdPin  = PIN_SPK_DIN;
    audioConfig.ampEnablePin = PIN_AMP_EN;
    audio.begin(audioConfig);

    // Cargar volumen persistido y sincronizar con AudioHandler y Display
    audio.setVolume(configMgr.config().volume);
    display.setVolumeLevel(configMgr.config().volume);
    Serial.printf("[Setup] Volumen: %d%%\n", configMgr.config().volume);

    Serial.println("[Setup] 6/7 Splash delay..."); Serial.flush();
    delay(2000);

    // --- Aplicar modo inicial ---
    // Forzar la transición: marcar temporalmente como OFF para que
    // applyDeviceMode() no haga early-return al comparar prevMode == newMode
    Serial.println("[Setup] 7/7 Aplicando modo..."); Serial.flush();
    DeviceMode savedMode = configMgr.config().deviceMode;
    configMgr.config().deviceMode = DeviceMode::OFF;
    applyDeviceMode(savedMode);

    // --- Crear tareas FreeRTOS ---

    // Tarea de UI en Core 1 (mismo core que Arduino loop)
    xTaskCreatePinnedToCore(
        taskUI,           // Función
        "TaskUI",         // Nombre
        8192,             // Stack (bytes)
        nullptr,          // Parámetros
        2,                // Prioridad (mayor = más prioritaria)
        &taskUIHandle,    // Handle
        1                 // Core 1
    );

    // Tarea de Red en Core 0
    xTaskCreatePinnedToCore(
        taskNetwork,
        "TaskNet",
        12288,            // Stack más grande para MQTT+HTTP
        nullptr,
        1,
        &taskNetHandle,
        0                 // Core 0
    );

    Serial.println("[Main] Setup completado. Tareas FreeRTOS iniciadas.\n");
}

// =============================================================================
// LOOP PRINCIPAL (Core 1 - Arduino default)
// =============================================================================

void loop() {
    DeviceMode currentMode = configMgr.config().deviceMode;

    // Audio: solo actualizar si no está en modo OFF
    if (currentMode != DeviceMode::OFF) {
        audio.update();
    }

    // Simulador: actualizar datos si está en modo SIMULADOR
    if (currentMode == DeviceMode::SIMULATOR) {
        if (xSemaphoreTake(pdiMutex, pdMS_TO_TICKS(10)) == pdTRUE) {
            simulator.update(pdiData);
            xSemaphoreGive(pdiMutex);
        }
    }

    // Mantener el LED actualizado
    led.update();

    delay(10); // Yield para watchdog
}

// =============================================================================
// TAREA DE UI (Display + Encoder) - Core 1
// =============================================================================

void taskUI(void* parameter) {
    (void)parameter;
    Serial.println("[TaskUI] Iniciada en Core 1");

    TickType_t lastWake = xTaskGetTickCount();

    for (;;) {
        // Leer encoder (no bloqueante)
        EncoderEvent event = encoder.read();
        display.handleInput(event);

        // Mostrar pantalla de audio si está grabando/reproduciendo/enviando
        AudioState audioSt = audio.getState();
        if (audioSt == AudioState::RECORDING || audioSt == AudioState::PLAYING
            || audioSt == AudioState::SENDING) {
            display.showAudioStatus(audioSt == AudioState::RECORDING,
                                    audio.getRecordingDurationMs());
        }
        // Actualizar display con datos PDI (solo si no estamos en pantalla de audio)
        else if (xSemaphoreTake(pdiMutex, pdMS_TO_TICKS(5)) == pdTRUE) {
            led.updateFromState(pdiData.status.stateCurrent);
            display.update(pdiData);
            xSemaphoreGive(pdiMutex);
        }

        // 50ms = 20 Hz de muestreo del encoder
        vTaskDelayUntil(&lastWake, pdMS_TO_TICKS(50));
    }
}

// =============================================================================
// TAREA DE RED (WiFi + MQTT + OTA) - Core 0
// =============================================================================

void taskNetwork(void* parameter) {
    (void)parameter;
    Serial.println("[TaskNet] Iniciada en Core 0");

    TickType_t lastWake = xTaskGetTickCount();

    for (;;) {
        DeviceMode mode = configMgr.config().deviceMode;

        if (mode == DeviceMode::OFF) {
            // Modo offline: no hacer nada de red
            vTaskDelay(pdMS_TO_TICKS(500));
            continue;
        }

        // Gestionar WiFi
        wifiPortal.update();

        // MQTT solo si WiFi está conectado
        if (wifiPortal.getState() == WifiState::CONNECTED) {
            mqtt.update();

            // Actualizar flag de conexión en pdiData
            if (xSemaphoreTake(pdiMutex, pdMS_TO_TICKS(5)) == pdTRUE) {
                pdiData.connectionStatus = mqtt.isConnected();
                xSemaphoreGive(pdiMutex);
            }
            // Los datos PDI se actualizan via callback onPDIReceived()
        }

        // 100ms entre iteraciones de red
        vTaskDelayUntil(&lastWake, pdMS_TO_TICKS(100));
    }
}

// =============================================================================
// CALLBACK DE COMANDOS MQTT
// =============================================================================

void onMqttCommand(const String& command, const String& payload) {
    Serial.printf("[Main] Comando MQTT: '%s' payload: '%s'\n",
                  command.c_str(), payload.c_str());

    if (command == "UPDATE") {
        // Iniciar OTA desde el servidor configurado
        display.showOTAProgress(0, "Iniciando OTA...");
        String firmwarePath = payload.length() > 0 ? payload : "/firmware.bin";
        OTAResult result = ota.startUpdate(firmwarePath);

        if (result != OTAResult::SUCCESS && result != OTAResult::IN_PROGRESS) {
            display.showMessage("OTA Error", ota.getLastError().c_str(), 5000);
            Serial.printf("[Main] OTA fallo: %s\n", ota.getLastError().c_str());
        }
        // Si es exitoso, el ESP se reinicia automáticamente

    } else if (command == "MODE") {
        if (payload == "OFF")       applyDeviceMode(DeviceMode::OFF);
        else if (payload == "ONLINE") applyDeviceMode(DeviceMode::ONLINE);
        else if (payload == "SIM")    applyDeviceMode(DeviceMode::SIMULATOR);

    } else if (command == "RESTART") {
        display.showMessage("Reiniciando", "...", 1000);
        delay(1500);
        ESP.restart();

    } else if (command == "VOLUME") {
        int vol = payload.toInt();
        if (vol >= 0 && vol <= 100) {
            audio.setVolume((uint8_t)vol);
            configMgr.setVolume((uint8_t)vol);
            display.setVolumeLevel((uint8_t)vol);
            Serial.printf("[Main] Volumen MQTT: %d%%\n", vol);
        }

    } else if (command == "STATUS") {
        // Log del estado actual (el dispositivo es visor, no publica PDI)
        Serial.printf("[Main] Estado actual: modo=%d, maquina=%d\n",
                      (int)pdiData.deviceMode, (int)pdiData.status.stateCurrent);
    }
}

// =============================================================================
// CALLBACK DE ACCIONES DEL MENÚ
// =============================================================================

void onMenuAction(uint8_t actionId) {
    Serial.printf("[Main] Accion del menu: %d\n", actionId);

    switch (actionId) {
        case ACTION_SET_MODE_OFF:
            applyDeviceMode(DeviceMode::OFF);
            display.showMessage("Modo OFF", "Offline", 1500);
            break;

        case ACTION_SET_MODE_ONLINE:
            applyDeviceMode(DeviceMode::ONLINE);
            display.showMessage("Modo ONLINE", "Conectando...", 1500);
            break;

        case ACTION_SET_MODE_SIM:
            applyDeviceMode(DeviceMode::SIMULATOR);
            display.showMessage("SIMULADOR", "Activo", 1500);
            break;

        case ACTION_START_PORTAL:
            wifiPortal.startPortal();
            display.showMessage("Portal WiFi", "192.168.4.1", 3000);
            break;

        case ACTION_FORCE_OTA:
            display.showOTAProgress(0, "Buscando update...");
            ota.startUpdate();
            break;

        case ACTION_RESET_CONFIG:
            configMgr.resetToDefaults();
            audio.setVolume(configMgr.config().volume);
            display.setVolumeLevel(configMgr.config().volume);
            display.showMessage("Config", "Reseteada!", 2000);
            break;

        case ACTION_VOLUME_CHANGED: {
            uint8_t vol = display.getVolumeLevel();
            audio.setVolume(vol);
            configMgr.setVolume(vol);
            Serial.printf("[Main] Volumen: %d%%\n", vol);
            break;
        }
    }
}

// =============================================================================
// APLICAR MODO DE OPERACIÓN
// =============================================================================

void applyDeviceMode(DeviceMode newMode) {
    DeviceMode prevMode = configMgr.config().deviceMode;
    if (prevMode == newMode) return;

    Serial.printf("[Main] Cambio de modo: %s -> %s\n",
                  deviceModeToStr(prevMode), deviceModeToStr(newMode));

    // Persistir el cambio
    configMgr.setDeviceMode(newMode);

    // Aplicar transiciones
    switch (newMode) {
        case DeviceMode::OFF:
            // Desconectar todo
            mqtt.disconnect();
            wifiPortal.disconnect();
            led.setColor(50, 50, 50);
            led.setPattern(LedPattern::BREATHE);
            if (xSemaphoreTake(pdiMutex, pdMS_TO_TICKS(50)) == pdTRUE) {
                pdiData.connectionStatus = false;
                pdiData.deviceMode = DeviceMode::OFF;
                xSemaphoreGive(pdiMutex);
            }
            break;

        case DeviceMode::ONLINE:
            // Conectar WiFi y MQTT
            if (xSemaphoreTake(pdiMutex, pdMS_TO_TICKS(50)) == pdTRUE) {
                pdiData.deviceMode = DeviceMode::ONLINE;
                xSemaphoreGive(pdiMutex);
            }
            wifiPortal.begin();
            mqtt.begin();
            break;

        case DeviceMode::SIMULATOR:
            // Iniciar simulador (WiFi+MQTT opcional para publicar datos sim)
            simulator.reset();
            if (xSemaphoreTake(pdiMutex, pdMS_TO_TICKS(50)) == pdTRUE) {
                pdiData.deviceMode = DeviceMode::SIMULATOR;
                xSemaphoreGive(pdiMutex);
            }
            // En modo simulador, intentar conectar para enviar datos al broker
            wifiPortal.begin();
            mqtt.begin();
            break;
    }
}
