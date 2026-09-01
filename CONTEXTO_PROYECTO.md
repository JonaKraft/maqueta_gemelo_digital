# Gemelo Digital Jose - Log de Contexto y Progreso

## Resumen del Proyecto
Prototipo industrial basado en **ESP32-S3-DevKitC-1 (N16R8)** que implementa un gemelo digital de maquinas industriales conforme a PackML/ISA-88. Funciona como visor/monitor remoto con pantalla OLED, encoder rotativo, LED RGB, audio PTT y conectividad WiFi+MQTT.

---

## Estado Actual del Firmware
- **Version:** 1.0.0
- **Framework:** Arduino + PlatformIO
- **Plataforma:** Espressif32 (ESP32-S3)

---

## Modulos Implementados (codigo escrito, pendiente de pruebas en hardware)

| Modulo | Archivo | Estado | Notas |
|---|---|---|---|
| Orquestador principal | main.cpp | Escrito | FreeRTOS dual-core |
| Datos PDI (PackML) | PDIData.h/cpp | Escrito | Estructura compartida con mutex |
| Config persistente | ConfigManager.h/cpp | Escrito | NVS (Preferences) |
| Cliente MQTT | MqttHandler.h/cpp | Escrito | Suscriptor/visor, buffer 32KB |
| WiFi + Portal cautivo | WifiPortal.h/cpp | Escrito | AP en 192.168.4.1 |
| OTA remoto | OTAUpdater.h/cpp | Escrito | Dual partition |
| Display OLED | DisplayMenu.h/cpp | Escrito | U8g2, SH1106 128x64, 10 paginas |
| Encoder rotativo | EncoderInput.h/cpp | Escrito | CLK/DT/SW con debounce+ISR |
| LED RGB | LedController.h/cpp | Escrito | NeoPixel WS2812, 4 patrones |
| Audio PTT | AudioHandler.h/cpp | Escrito | I2S, WAV 16kHz mono, max 20s |
| Simulador | Simulator.h/cpp | Escrito | Datos sinteticos coherentes |

---

## Asignacion de Pines ESP32-S3

```
COMPONENTE          PIN ESP32-S3    NOTAS
─────────────────────────────────────────────────────
Encoder CLK         GPIO 39         Senal de reloj
Encoder DT          GPIO 40         Senal de datos (direccion)
Encoder SW          GPIO 41         Pulsador integrado (push button)
OLED SDA            GPIO 5          I2C Data
OLED SCL            GPIO 6          I2C Clock
LED NeoPixel        GPIO 48         WS2812 onboard del DevKitC-1
Audio PTT Button    GPIO 4          Push-to-Talk
Mic I2S WS          GPIO 15         Word Select (INMP441)
Mic I2S SCK         GPIO 16         Serial Clock (INMP441)
Mic I2S SD          GPIO 17         Serial Data (INMP441)
Spk I2S BCLK       GPIO 7          Bit Clock (MAX98357A)
Spk I2S LRC         GPIO 8          LRCLK (MAX98357A)
Spk I2S DIN         GPIO 18         Data In (MAX98357A)
```

**GPIOs NO disponibles:** 33-37 (usados por PSRAM octal)
**GPIOs reservados:** 19/20 (USB CDC), 43/44 (UART0)

---

## Arquitectura FreeRTOS

```
CORE 0 - taskNetwork (prioridad 1):
  WiFi + MQTT + OTA

CORE 1 - taskUI (prioridad 2):
  Display OLED + Encoder + LED RGB

Loop principal (Core 1):
  Audio PTT + Simulador + Coordinacion de modos
```

---

## Modos de Operacion

1. **OFF** - Desconectado, LED respira gris, display funcional offline
2. **ONLINE** - Conectado a broker MQTT, recibe datos reales de maquina
3. **SIMULADOR** - Genera datos sinteticos para demo sin maquina

---

## Log de Sesiones de Trabajo

### Sesion 1 - 27/Jun/2026
- Se escribio TODO el firmware base completo (todos los modulos)
- Ultimo archivo modificado: main.cpp y MqttHandler.cpp (20:05)
- Estado: codigo completo, sin pruebas en hardware real

### Sesion 2 - 30/Jun/2026
- Revision general del proyecto para retomar contexto
- Creacion de este archivo de contexto/log
- Revision de pines para conexion de OLED y encoder
- Rotacion del display 180 grados (U8G2_R0 -> U8G2_R2 en DisplayMenu.cpp)
- Fix: publishPDI no existe -> reemplazado por log serial (main.cpp)
- Fix: machineState -> status.stateCurrent (main.cpp)
- Implementacion de reproduccion de audio con MAX98357A:
  - Pines speaker definidos: BCLK=GPIO7, LRC=GPIO8, DIN=GPIO18
  - setupI2SOutput() implementado en AudioHandler.cpp (I2S_NUM_1)
  - playLastRecording() implementado: lee WAV de LittleFS y envia a speaker
  - Auto-reproduccion tras grabar (para verificar sin MQTT)
- **Siguiente paso:** Conectar INMP441 + PTT + MAX98357A y probar audio

---

## Pendientes / Roadmap

- [x] Conectar y probar display OLED (I2C en GPIO 5/6)
- [x] Conectar y probar encoder rotativo (GPIO 39/40/41)
- [x] Verificar navegacion del menu en pantalla
- [x] Rotacion de display 180 grados (U8G2_R2)
- [x] Corregir error publishPDI (dispositivo es visor, no publica PDI)
- [x] Corregir campo machineState -> status.stateCurrent
- [ ] Conectar microfono INMP441 (I2S en GPIO 15/16/17)
- [ ] Conectar boton PTT (GPIO 4)
- [ ] Conectar speaker MAX98357A (I2S en GPIO 7/8/18)
- [ ] Verificar grabacion y reproduccion de audio
- [ ] Compilar y flashear firmware al ESP32-S3
- [ ] Probar modo SIMULADOR (no requiere MQTT)
- [ ] Configurar WiFi via portal cautivo
- [ ] Probar conexion MQTT con broker
- [ ] Probar LED NeoPixel onboard (GPIO 48)
- [ ] Probar OTA update

---

## Conexiones Fisicas (Guia de Cableado)

### Display OLED 1.3" SH1106/SSD1306 (I2C)
```
OLED Pin    ->    ESP32-S3 Pin
────────────────────────────────
VCC         ->    3.3V
GND         ->    GND
SDA         ->    GPIO 5
SCL         ->    GPIO 6
```

### Encoder Rotativo (KY-040 o similar, 5 pines)
```
Encoder Pin (3 del encoder)    ->    ESP32-S3 Pin
─────────────────────────────────────────────────────
CLK                            ->    GPIO 39
DT                             ->    GPIO 40
GND (encoder)                  ->    GND

Encoder Pin (2 del pulsador)   ->    ESP32-S3 Pin
─────────────────────────────────────────────────────
SW                             ->    GPIO 41
GND (pulsador)                 ->    GND (compartido)
```
**Nota:** El encoder no necesita VCC externo. Los pines CLK, DT y SW usan INPUT_PULLUP interno del ESP32-S3.

### Microfono INMP441 (I2S digital)
```
INMP441 Pin    ->    ESP32-S3 Pin
──────────────────────────────────
VDD            ->    3.3V
GND            ->    GND
WS  (LRCLK)   ->    GPIO 15
SCK (BCLK)     ->    GPIO 16
SD  (DOUT)     ->    GPIO 17
L/R            ->    GND  (canal izquierdo)
```

### Boton PTT (Push-to-Talk)
```
Pulsador       ->    ESP32-S3 Pin
──────────────────────────────────
Un pin         ->    GPIO 4
Otro pin       ->    GND
```
**Nota:** Usa INPUT_PULLUP interno. No necesita resistencia.

### Speaker MAX98357A + Parlante 2W
```
MAX98357A Pin  ->    ESP32-S3 Pin
──────────────────────────────────
VIN            ->    5V
GND            ->    GND
BCLK           ->    GPIO 7
LRC            ->    GPIO 8
DIN            ->    GPIO 18
GAIN           ->    sin conectar (9dB default)
SD             ->    sin conectar (activo default)

Parlante:
(+)            ->    MAX98357A (+)
(-)            ->    MAX98357A (-)
```
