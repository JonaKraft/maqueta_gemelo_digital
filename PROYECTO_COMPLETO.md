# Gemelo Digital Industrial - ESP32-S3

## Resumen ejecutivo

Prototipo industrial basado en **ESP32-S3-DevKitC-1 (N16R8)** que implementa un gemelo digital de maquinas industriales conforme a PackML/ISA-88. Funciona como **visor/monitor remoto**: recibe datos de maquinas via MQTT y los muestra en pantalla OLED con navegacion por encoder rotativo. Incluye audio Push-to-Talk bidireccional, LED RGB indicador de estado, simulador de datos para demo, portal WiFi cautivo para configuracion, y actualizacion OTA.

**No es un controlador.** El dispositivo solo visualiza y transmite audio. No envia comandos a la maquina.

---

## Estructura del proyecto

```
Gemelo Digital Jose/
├── src/                           Codigo fuente principal
│   ├── main.cpp                   Punto de entrada, orquestacion FreeRTOS
│   ├── PDIData.h / .cpp           Estructuras de datos PackML y enums
│   ├── ConfigManager.h / .cpp     Almacenamiento persistente NVS
│   ├── MqttHandler.h / .cpp       Cliente MQTT (modelo suscriptor)
│   ├── WifiPortal.h / .cpp        WiFi STA + portal cautivo AP
│   ├── OTAUpdater.h / .cpp        Actualizacion firmware por HTTP
│   ├── DisplayMenu.h / .cpp       Sistema de menus OLED 128x64
│   ├── EncoderInput.h / .cpp      Encoder rotativo con ISR
│   ├── LedController.h / .cpp     LED NeoPixel WS2812
│   ├── AudioHandler.h / .cpp      Audio I2S (mic INMP441 + speaker MAX98357A)
│   └── Simulator.h / .cpp         Generador de datos sinteticos
├── data/                          Sistema de archivos LittleFS (audio temporal)
├── platformio.ini                 Configuracion de compilacion
└── CONTEXTO_PROYECTO.md           Documentacion original del proyecto
```

---

## Hardware

### Microcontrolador

- **Placa:** ESP32-S3-DevKitC-1 N16R8
- **CPU:** Dual-core Xtensa LX7 @ 240 MHz
- **Flash:** 16 MB QIO
- **PSRAM:** 8 MB octal SPI (GPIOs 33-37 reservados)
- **USB:** CDC nativo (GPIOs 19/20 reservados)
- **UART0:** GPIOs 43/44 reservados

### Asignacion de pines

| Componente | GPIO | Funcion |
|------------|------|---------|
| Encoder CLK | 39 | Entrada rotativa (ISR) |
| Encoder DT | 40 | Senal de direccion |
| Encoder SW | 41 | Pulsador del encoder |
| OLED SDA | 5 | I2C datos |
| OLED SCL | 6 | I2C reloj |
| LED NeoPixel | 48 | WS2812 datos (LED onboard) |
| PTT Button | 4 | Push-to-Talk (INPUT_PULLUP, activo LOW) |
| Mic I2S WS | 15 | INMP441 Word Select |
| Mic I2S SCK | 16 | INMP441 Bit Clock |
| Mic I2S SD | 17 | INMP441 Serial Data |
| Speaker BCLK | 7 | MAX98357A Bit Clock |
| Speaker LRC | 8 | MAX98357A Word Select |
| Speaker DIN | 18 | MAX98357A Data In |
| Amp Enable | 10 | BC337 base (activo HIGH durante playback) |

### Perifericos

- **Display:** OLED SH1106 128x64, I2C, rotacion 180 grados (U8G2_R2)
- **Encoder:** KY-040 o compatible, con pulsador
- **Microfono:** INMP441 MEMS, I2S, 24-bit en frame de 32-bit
- **Amplificador 1:** MAX98357A (DAC I2S + amplificador clase D)
- **Amplificador 2:** PAM8403 (amplificador clase D analogico, en serie con MAX98357A)
- **Transistor:** BC337 en GPIO 10 para habilitar amplificador durante reproduccion
- **LED:** WS2812 NeoPixel onboard del DevKitC-1

### Conexion de audio (cadena de amplificacion)

```
ESP32 (I2S) --> MAX98357A (3.3V) --> PAM8403 (5V) --> Parlante
                                  ^
                          Nota: doble amplificacion puede saturar.
                          Usar divisor de tension entre MAX98357A y PAM8403
                          o bajar GAIN del MAX98357A (100K a GND = 6dB min)
```

---

## Configuracion de compilacion (platformio.ini)

```ini
[env:esp32s3]
platform = espressif32
board = esp32-s3-devkitc-1
framework = arduino
monitor_speed = 115200
upload_speed = 921600
board_build.flash_mode = qio

build_flags =
    -DBOARD_HAS_PSRAM
    -DARDUINO_USB_MODE=0
    -DARDUINO_USB_CDC_ON_BOOT=1
    -DCONFIG_ARDUINO_LOOP_STACK_SIZE=8192
    -DUSE_NEOPIXEL_LED
    -DVERSION_MAJOR=1
    -DVERSION_MINOR=0
    -DVERSION_PATCH=0

lib_deps =
    olikraus/U8g2@^2.35.19
    knolleary/PubSubClient@^2.8
    bblanchon/ArduinoJson@^7.1.0
    adafruit/Adafruit NeoPixel@^1.12.3
```

---

## Arquitectura de software

### Tareas FreeRTOS

```
Core 0: taskNetwork (prioridad 1, stack 12KB, cada 100ms)
  └── WiFi + MQTT + OTA

Core 1: taskUI (prioridad 2, stack 8KB, cada 50ms)
  └── Display OLED + Encoder + LED sync

Core 1: loop() (prioridad 1, stack 8KB, cada 10ms)
  └── Audio PTT + Simulador + LED update
```

### Instancias globales

```cpp
ConfigManager configMgr;       // Almacenamiento persistente NVS
EncoderInput  encoder;          // Entrada rotativa
LedController led;              // LED RGB
WifiPortal    wifiPortal;       // WiFi + portal cautivo
MqttHandler   mqtt;             // Cliente MQTT
OTAUpdater    ota;              // Actualizacion firmware
AudioHandler  audio;            // Audio I2S bidireccional
Simulator     simulator;        // Datos sinteticos
DisplayMenu   display;          // Menu OLED

PDIData       pdiData;          // Estructura de datos compartida
SemaphoreHandle_t pdiMutex;     // Mutex para acceso concurrente a pdiData
```

### Protocolo de mutex

`pdiData` esta protegida por `pdiMutex` (semaforo binario):
- **Escritores:** callback MQTT (taskNetwork), Simulator (loop)
- **Lectores:** Display (taskUI), consultas de estado
- **Timeout:** 5-50ms segun contexto
- **Patron:** `xSemaphoreTake()` -> leer/escribir -> `xSemaphoreGive()`

### Comunicacion entre modulos (callbacks)

```
MQTT --> onPDIReceived(PDIData) --> actualiza pdiData via mutex
MQTT --> onCommand(cmd, payload) --> ejecuta comando
Menu --> onAction(actionId) --> aplica accion (modo, portal, OTA, reset)
OTA  --> onProgress(current, total) --> actualiza display
```

---

## Estructuras de datos (PDIData.h)

### Enumeraciones

```cpp
enum class MachineState : uint8_t {
    EXECUTING = 0,  // Verde - Maquina ejecutando
    STOPPED   = 1,  // Rojo - Maquina detenida
    IDLE      = 2,  // Amarillo - En espera
    HELD      = 3,  // Azul - Retenida
    ABORTED   = 4   // Rojo parpadeo - Abortada
};

enum class UnitMode : uint8_t {
    AUTO   = 0,     // Operacion automatica
    MANUAL = 1,     // Operacion manual
    SETUP  = 2      // Configuracion
};

enum class QualitySignal : uint8_t {
    OK        = 0,  // Datos confiables
    BAD       = 1,  // Datos malos
    UNCERTAIN = 2   // Datos inciertos
};

enum class DeviceMode : uint8_t {
    OFF       = 0,  // Sin red, offline
    ONLINE    = 1,  // Conectado a MQTT
    SIMULATOR = 2   // Datos sinteticos
};
```

### Estructura principal PDIData

```cpp
struct PDI_Status {
    MachineState stateCurrent;     // Estado actual de la maquina
    MachineState statePrevious;    // Estado anterior
    uint32_t stateDuration;        // Segundos en estado actual
    UnitMode unitModeCurrent;      // AUTO/MANUAL/SETUP
    float machSpeed;               // Velocidad nominal
    float curMachSpeed;            // Velocidad actual
};

struct PDI_Admin {
    uint32_t prodProcessedCount;   // Piezas procesadas totales
    uint32_t prodDefectiveCount;   // Piezas defectuosas
    // goodCount() = prodProcessedCount - prodDefectiveCount
};

struct PDI_Context {
    String orderNo;                // Numero de orden
    String productCode;            // Codigo de producto
    String recipeId;               // ID de receta
    String lotBatch;               // Lote
    String lineId;                 // Linea de produccion
};

struct PDI_Alarms {
    String stopReasonCode;         // Codigo de parada
    String stopReasonDesc;         // Descripcion de parada
    bool alarmActive;              // Flag de alarma
    String alarmCode;              // Codigo de alarma
    String alarmText;              // Texto de alarma
};

struct PDI_Measurements {
    float power;                   // Potencia en Watts
    float current;                 // Corriente en Amps
    float airPressure;             // Presion de aire en bar
};

struct PDIData {
    String equipmentName;
    String assetId;
    uint32_t timestamp;
    PDI_Status status;
    PDI_Admin admin;
    QualitySignal qualitySignal;
    bool connectionStatus;         // MQTT conectado
    DeviceMode deviceMode;         // Modo actual del dispositivo
    PDI_Context context;
    PDI_Alarms alarms;
    PDI_Measurements measurements;
};
```

### Funciones de conversion

```cpp
const char* machineStateToStr(MachineState state);
const char* unitModeToStr(UnitMode mode);
const char* qualitySignalToStr(QualitySignal signal);
const char* deviceModeToStr(DeviceMode mode);
MachineState strToMachineState(const String& str);
```

---

## Modulo: ConfigManager

Almacenamiento persistente en NVS (Preferences) del ESP32.

### Estructura DeviceConfig

```cpp
struct DeviceConfig {
    String wifiSSID;               // Red WiFi
    String wifiPassword;
    String mqttHost;               // Broker MQTT
    uint16_t mqttPort;             // Default: 1883
    String mqttUser;               // Autenticacion (futuro)
    String mqttPassword;
    String equipmentName;          // ID del equipo, usado en topics MQTT
    String assetId;                // ID del activo
    String otaUrl;                 // Servidor OTA
    uint16_t otaPort;              // Default: 8080
    DeviceMode deviceMode;         // Modo guardado
};
```

### Valores por defecto

```
WiFi SSID:        ""  (vacio -> lanza portal en primer arranque)
WiFi Password:    ""
MQTT Host:        "broker.colmena.io"
MQTT Port:        1883
OTA URL:          "http://192.168.1.100"
OTA Port:         8080
Equipment Name:   "ESP32-GD-001"
Asset ID:         "ASSET-001"
Device Mode:      ONLINE
```

### Namespace NVS: `"gemelo_cfg"`

### API

```cpp
void begin();                                  // Cargar config desde NVS
void save();                                   // Persistir toda la config
void resetToDefaults();                        // Factory reset
void setWifiCredentials(ssid, pass);           // Guardar WiFi
void setMqttParams(host, port);                // Guardar MQTT
void setOtaParams(url, port);                  // Guardar OTA
void setDeviceMode(mode);                      // Guardar modo
void setEquipmentName(name);                   // Guardar nombre equipo
DeviceConfig& config();                        // Referencia a config activa
```

---

## Modulo: MqttHandler

Cliente MQTT en modelo suscriptor. Recibe datos de maquinas desde un publicador externo (PLC, SCADA, gateway). Solo publica: estado del dispositivo y audio.

### Configuracion

- **Buffer MQTT:** 32 KB
- **Reconnect:** cada 5 segundos
- **Keep-alive:** 60 segundos

### Topics MQTT

Base: `colmena/gemelo/{equipmentName}`

**Suscripciones (recibe):**

| Topic | Contenido |
|-------|-----------|
| `.../pdi` | JSON completo con datos PackML de la maquina |
| `.../control` | Comandos: UPDATE, MODE, RESTART, STATUS |

**Publicaciones (envia):**

| Topic | Contenido |
|-------|-----------|
| `.../device/status` | `{"online":true/false}` (con LWT) |
| `.../audio/data/{chunkIdx}` | Datos binarios de audio |
| `.../audio/meta` | `{"chunk":idx, "total":chunks, "size":bytes, "ts":ms}` |

### Formato JSON del PDI recibido

```json
{
  "EquipmentName": "Machine-A",
  "AssetId": "ASSET-001",
  "Timestamp": 1719936000,
  "Status": {
    "StateCurrent": "Ejecutando",
    "StatePrevious": "Idle",
    "StateDuration": 45,
    "UnitModeCurrent": "Auto",
    "MachSpeed": 120.0,
    "CurMachSpeed": 98.5
  },
  "Admin": {
    "ProdProcessedCount": {"Count": 1250},
    "ProdDefectiveCount": {"Count": 8}
  },
  "QualitySignal": "OK",
  "Context": {
    "OrderNo": "ORD-2026-001",
    "ProductCode": "PROD-A",
    "RecipeId": "RCP-001",
    "LotBatch": "LOT-001",
    "LineId": "LINE-01"
  },
  "Alarms": {
    "AlarmActive": false,
    "AlarmCode": "E-001",
    "AlarmText": "Motor overheat",
    "StopReasonCode": "SR-01",
    "StopReasonDesc": "Maintenance"
  },
  "Measurements": {
    "Power": 1850.5,
    "Current": 8.4,
    "AirPressure": 5.8
  }
}
```

### Valores de StateCurrent reconocidos

`"Ejecutando"`, `"Detenido"`, `"Idle"`, `"Held"`, `"Abortado"`

### API

```cpp
void begin();
void update();                     // Mantener conexion + procesar mensajes
void disconnect();
bool isConnected();
void onPDIReceived(callback);      // Registrar callback de datos
void onCommand(callback);          // Registrar callback de comandos
void publishAudioChunk(data, len, chunkIdx, totalChunks);
```

### Callbacks

```cpp
using MqttPDICallback = std::function<void(const PDIData& data)>;
using MqttCommandCallback = std::function<void(const String& cmd, const String& payload)>;
```

---

## Modulo: WifiPortal

Gestion WiFi no-bloqueante con portal cautivo para configuracion inicial.

### Estados

```cpp
enum class WifiState {
    DISCONNECTED,    // WiFi apagado
    CONNECTING,      // Intentando conexion STA (30s timeout)
    CONNECTED,       // Conectado a red WiFi
    AP_ACTIVE,       // Portal cautivo activo
    AP_CONFIGURED    // Usuario envio config, reconectando
};
```

### Portal cautivo

- **SSID del AP:** `"GemeloDigital-Setup"`
- **IP:** `192.168.4.1`
- **DNS cautivo:** redirige todo al AP
- **Servidor web:** puerto 80 con formulario HTML
- **Timeout STA:** 30 segundos, luego lanza portal
- **Reconnect:** cada 15 segundos

### Campos del formulario del portal

```
ssid, password          WiFi
mqtt_host, mqtt_port    Broker MQTT
ota_url, ota_port       Servidor OTA
equip_name              Nombre del equipo
dev_mode                0=OFF, 1=ONLINE, 2=SIMULATOR
```

### API

```cpp
void begin();                  // Intentar conexion STA
void update();                 // Maquina de estados + DNS/HTTP
void startPortal();            // Lanzar AP + portal
void stopPortal();             // Cerrar AP
void disconnect();             // Apagar WiFi
WifiState getState();
String getIP();
int32_t getRSSI();
bool isPortalActive();
```

---

## Modulo: OTAUpdater

Actualizacion de firmware por HTTP desde servidor configurado.

### Esquema de particiones: OTA dual (OTA_0, OTA_1)

### Resultado

```cpp
enum class OTAResult {
    SUCCESS,        // Actualizado, reiniciando
    HTTP_ERROR,     // Error de conexion/descarga
    WRITE_ERROR,    // Error de escritura en flash
    VERIFY_ERROR,   // Fallo verificacion MD5
    NO_UPDATES,     // Sin actualizacion disponible
    IN_PROGRESS     // Actualizacion en curso
};
```

### API

```cpp
OTAResult startUpdate(firmwarePath = "/firmware.bin");
void onProgress(callback);        // callback(size_t current, size_t total)
bool isUpdating();
uint8_t getProgress();             // 0-100%
String getLastError();
```

---

## Modulo: DisplayMenu

Sistema de menus para OLED SH1106/SSD1306 128x64 via I2C.

### Paginas del display

```cpp
enum class DisplayPage {
    SPLASH,          // Pantalla de arranque
    DASHBOARD,       // Vista principal: estado + velocidad
    STATUS_DETAIL,   // Estado, duracion, velocidades
    PRODUCTION,      // Contadores: total, buenos, defectos
    MENU_MAIN,       // Menu principal (5 items)
    MENU_MODE,       // Selector de modo (OFF/ONLINE/SIM)
    MENU_WIFI,       // Info WiFi + lanzar portal
    MENU_OTA,        // Info OTA + forzar update
    OTA_PROGRESS,    // Barra de progreso OTA
    AUDIO_STATUS     // Estado de grabacion/reproduccion
};
```

### Items del menu principal

1. "Modo" -> selector OFF/ONLINE/SIM
2. "WiFi/Portal" -> info red + lanzar portal
3. "OTA Update" -> info OTA + forzar actualizacion
4. "Info Sistema" -> popup con info del dispositivo
5. "Reset Config" -> factory reset

### Navegacion por encoder

- **Girar CW:** siguiente item / aumentar valor
- **Girar CCW:** item anterior / disminuir valor
- **Press corto:** seleccionar / confirmar
- **Press largo (>1s):** volver / salir del menu

### IDs de acciones (callbacks al main)

```cpp
#define ACTION_SET_MODE_OFF     1
#define ACTION_SET_MODE_ONLINE  2
#define ACTION_SET_MODE_SIM     3
#define ACTION_START_PORTAL     4
#define ACTION_FORCE_OTA        5
#define ACTION_RESET_CONFIG     6
```

### Configuracion del display

- **Driver:** U8G2_SH1106_128X64_NONAME_F_HW_I2C
- **Rotacion:** U8G2_R2 (180 grados)
- **Refresh:** 100ms (10 FPS)
- **Fuente header:** u8g2_font_6x10_tr
- **Fuente titulo:** u8g2_font_helvB12_tr

### API

```cpp
void begin();
void handleInput(EncoderEvent event);
void update(const PDIData& data);
void showSplash(const char* version);
void showOTAProgress(uint8_t percent, const char* status);
void showAudioStatus(bool recording, uint32_t durationMs);
void showMessage(const char* line1, const char* line2, uint16_t durationMs);
void onAction(callback);          // callback(uint8_t actionId)
```

---

## Modulo: EncoderInput

Encoder rotativo KY-040 con decodificacion por cuadratura e ISR.

### Eventos

```cpp
enum class EncoderEvent {
    NONE,           // Sin evento
    ROTATE_CW,      // Giro horario (posicion += 2)
    ROTATE_CCW,     // Giro antihorario (posicion -= 2)
    BUTTON_PRESS,   // Press corto (<1s)
    BUTTON_LONG     // Press largo (>=1s)
};
```

### Configuracion

- **Debounce:** 50ms
- **Long press:** >= 1000ms
- **Threshold rotacion:** 2 incrementos (detent mecanico)
- **ISR:** IRAM_ATTR en CLK y DT, decodificacion Gray code
- **Pines:** todos con INPUT_PULLUP

### API

```cpp
void begin();                      // Configurar pines, adjuntar ISRs
EncoderEvent read();               // Poll no-bloqueante
int32_t getPosition();             // Posicion absoluta
void resetPosition();              // Resetear a cero
```

---

## Modulo: LedController

Control de LED NeoPixel WS2812 (o PWM RGB alternativo).

### Flag de compilacion: `-DUSE_NEOPIXEL_LED` (activo por defecto)

### Patrones de animacion

```cpp
enum class LedPattern {
    SOLID,          // Color constante
    BLINK,          // 1 Hz (500ms on/off)
    FAST_BLINK,     // 4 Hz (125ms on/off)
    BREATHE,        // Fade in/out suave
    OFF             // Apagado
};
```

### Mapeo de color segun estado de maquina

| MachineState | Color | Patron |
|-------------|-------|--------|
| EXECUTING | Verde (0,255,0) | SOLID |
| STOPPED | Rojo (255,0,0) | SOLID |
| IDLE | Amarillo (255,200,0) | BREATHE |
| HELD | Azul (0,0,255) | BLINK |
| ABORTED | Rojo (255,0,0) | FAST_BLINK |

### API

```cpp
void begin();
void setColor(uint8_t r, uint8_t g, uint8_t b);
void setPattern(LedPattern pattern);
void updateFromState(MachineState state);   // Auto color+patron
void update();                              // Procesar animacion
void off();
```

---

## Modulo: AudioHandler

Audio I2S bidireccional: grabacion PTT desde microfono INMP441 y reproduccion por MAX98357A. Arquitectura completamente no-bloqueante basada en maquina de estados.

### Configuracion de audio

```cpp
struct AudioConfig {
    uint32_t sampleRate    = 16000;    // 16 kHz
    uint8_t  bitsPerSample = 16;      // 16-bit PCM
    uint8_t  channels      = 1;       // Mono
    uint32_t maxDurationMs = 20000;   // 20 segundos maximo
    uint8_t  pttPin;                  // Boton PTT
    uint8_t  i2sWsPin, i2sSckPin, i2sSdPin;        // Pines mic
    uint8_t  i2sSpkWsPin, i2sSpkSckPin, i2sSpkSdPin;  // Pines speaker
    uint8_t  ampEnablePin  = 0xFF;    // Pin BC337 (0xFF = no configurado)
};
```

### Estados

```cpp
enum class AudioState : uint8_t {
    IDLE       = 0,    // Sin actividad
    RECORDING  = 1,    // PTT presionado, grabando
    PLAYING    = 3,    // Reproduciendo por speaker
    SENDING    = 2     // Enviando chunks por MQTT
};
```

### I2S

- **I2S_NUM_0 (mic):** master RX, 32-bit frames, solo canal izquierdo, DMA 4x128
- **I2S_NUM_1 (speaker):** master TX, 16-bit PCM, solo canal izquierdo, DMA 8x256
- Ambos se inicializan al arrancar pero se mantienen parados (i2s_stop) hasta que se necesitan

### Flujo de audio (no-bloqueante)

```
IDLE
  │ PTT presionado (100ms debounce)
  v
RECORDING
  │ i2s_read() 256 samples de 32-bit (timeout 50ms)
  │ Convertir a 16-bit: sample >> 16
  │ Escribir a LittleFS: /audio_temp.raw
  │ Max 20 segundos
  │ PTT soltado o timeout
  v
stopRecording()
  │ i2s_stop(I2S_NUM_0)
  │ Escribir header WAV (44 bytes) al inicio del archivo
  │ Cerrar archivo
  v
PLAYING (playLastRecording)
  │ Abrir archivo, saltar header WAV
  │ digitalWrite(ampEnablePin, HIGH)
  │ i2s_start(I2S_NUM_1)
  │ updatePlayback(): leer 512 bytes por llamada
  │ i2s_write() con timeout 50ms
  │ Fin de archivo
  v
  │ i2s_stop(I2S_NUM_1)
  │ digitalWrite(ampEnablePin, LOW)
  │ Si MQTT conectado:
  v
SENDING (updateSending)
  │ Leer 4096 bytes por llamada
  │ mqtt.publishAudioChunk() cada 50ms
  │ Fin de archivo
  v
IDLE
```

### Formato WAV

- RIFF/WAVE PCM (AudioFormat=1)
- 16 kHz, 16-bit, mono
- Header de 44 bytes escrito al cerrar la grabacion

### Almacenamiento

- **LittleFS** montado en `/`
- Archivo temporal: `/audio_temp.raw`
- Se sobreescribe en cada grabacion

### API

```cpp
void begin(const AudioConfig& config);
void update();                          // Maquina de estados principal
AudioState getState();
uint32_t getRecordingDurationMs();
void beginPlayback();
void playLastRecording();
void stopPlayback();
```

---

## Modulo: Simulator

Generador de datos sinteticos coherentes para demo sin hardware real.

### Parametros

```cpp
MAX_SPEED          = 120.0f     // RPM o unidades/min
SPEED_RAMP_RATE    = 2.5f       // Unidades por segundo
DEFECT_RATE        = 0.03f      // 3% defectos
MIN_STATE_DURATION = 8000ms     // Minimo en un estado
MAX_STATE_DURATION = 30000ms    // Maximo en un estado
```

### Maquina de estados

```
IDLE --> EXECUTING (80%) | STOPPED (20%)
EXECUTING --> IDLE (60%) | STOPPED (40%)
STOPPED --> IDLE (100%)
HELD/ABORTED --> IDLE (100%)
```

Minimo 8 segundos por estado. La probabilidad de transicion aumenta con el tiempo hasta MAX (30s).

### Simulacion de velocidad

- EXECUTING: 70-100% de MAX_SPEED con rampa
- IDLE: 0-15% de MAX_SPEED
- STOPPED/HELD/ABORTED: 0 RPM
- Rampa: 2.5 u/s aceleracion, 5 u/s desaceleracion
- Ruido: +/- 0.5 unidades

### Simulacion de produccion

- Piezas/segundo = curMachSpeed / 60.0
- Solo incrementa durante EXECUTING con speed > 10
- Defectos: 3% del total

### Simulacion de mediciones

```
Potencia = speedRatio * 2200W +/- 50W
Corriente = Power / 220V +/- 0.2A
Presion aire = 6.0 bar +/- 0.3 bar
```

### Calidad simulada

- Speed > 50% MAX -> OK
- Speed > 20% MAX -> UNCERTAIN
- Speed <= 20% en EXECUTING -> BAD

### API

```cpp
void reset();
void update(PDIData& data);    // Genera siguiente paso y llena PDIData
```

---

## Flujos principales

### Secuencia de inicializacion (setup)

1. Serial 115200, esperar USB CDC (max 3s)
2. ConfigManager.begin() - cargar NVS
3. Encoder.begin() - adjuntar ISRs
4. LED.begin() - NeoPixel init
5. Display.begin() - OLED init
6. Splash screen "v1.0.0" + LED azul breathe
7. Inicializar pdiData con defaults
8. Registrar callbacks (MQTT, OTA, menu)
9. Audio.begin() - I2S mic + speaker
10. Delay 2s (splash)
11. Aplicar modo guardado (OFF/ONLINE/SIM)
12. Crear taskUI en Core 1
13. Crear taskNetwork en Core 0

### Transiciones de modo

**OFF:**
- Desconectar MQTT y WiFi
- LED gris (50,50,50) breathe
- pdiData.connectionStatus = false
- Audio habilitado pero sin envio MQTT

**ONLINE:**
- Iniciar WiFi (STA, 30s timeout -> portal)
- Iniciar MQTT
- LED segun estado de maquina recibido
- Audio graba + envia por MQTT

**SIMULATOR:**
- Reset simulador
- Iniciar WiFi + MQTT (opcional, para publicar datos simulados)
- LED segun estado simulado
- Audio habilitado

### Comandos MQTT (topic .../control)

| Comando | Payload | Accion |
|---------|---------|--------|
| `UPDATE` | `"/firmware.bin"` (opcional) | Iniciar OTA |
| `MODE` | `"OFF"` / `"ONLINE"` / `"SIM"` | Cambiar modo |
| `RESTART` | - | Reiniciar ESP32 |
| `STATUS` | - | Log estado a serial |

### Acciones del menu

| Action ID | Descripcion |
|-----------|-------------|
| 1 | Modo OFF |
| 2 | Modo ONLINE |
| 3 | Modo SIMULADOR |
| 4 | Lanzar portal WiFi |
| 5 | Forzar OTA |
| 6 | Reset configuracion |

---

## Patrones de diseno

1. **Arquitectura no-bloqueante:** todos los modulos usan polling o maquinas de estado. Sin delays en caminos criticos. I2S usa DMA.

2. **Datos compartidos con mutex:** PDIData es la unica fuente de verdad, protegida por semaforo binario para acceso desde multiples cores.

3. **Comunicacion por callbacks:** desacoplamiento entre modulos. MQTT, menu y OTA notifican via funciones registradas.

4. **Inicializacion condicional:** WiFi/MQTT solo si modo != OFF. Simulador solo si modo == SIMULATOR. Audio siempre disponible.

5. **Persistencia inmediata:** cada setter de ConfigManager guarda a NVS inmediatamente.

6. **Aislamiento por core:** red en Core 0, UI en Core 1, audio en loop de Core 1.

---

## Notas tecnicas importantes

- El dispositivo es **solo visor**. No publica datos PDI ni controla maquinas.
- Los GPIOs 33-37 estan reservados para PSRAM octal. No usar.
- Los GPIOs 19/20 estan reservados para USB CDC. No usar.
- El I2S del speaker se inicializa al arrancar (no lazy) para evitar chasquidos por pines flotantes.
- La grabacion usa `i2s_read` con timeout de 50ms (no `portMAX_DELAY`) para no bloquear el loop.
- La reproduccion y envio MQTT son no-bloqueantes: procesan un chunk por cada llamada a `update()`.
- El archivo de audio temporal se almacena en LittleFS y se sobreescribe en cada grabacion.
- La cadena de amplificacion MAX98357A + PAM8403 puede saturar. Se recomienda divisor de tension entre ambos o reducir GAIN del MAX98357A.
