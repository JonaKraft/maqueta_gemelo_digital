/**
 * @file DisplayMenu.h
 * @brief Sistema de menú y visualización para OLED 1.3" (SH1106/SSD1306)
 *
 * Usa la librería U8g2 para renderizado. Implementa un menú jerárquico
 * navegable con encoder rotativo. Muestra datos PDI en tiempo real
 * y permite configurar el modo del dispositivo.
 */

#ifndef DISPLAY_MENU_H
#define DISPLAY_MENU_H

#include <Arduino.h>
#include <WiFi.h>
#include <U8g2lib.h>
#include <Wire.h>
#include "PDIData.h"
#include "EncoderInput.h"
#include "ConfigManager.h"

/** Pantallas/Páginas del display */
enum class DisplayPage : uint8_t {
    SPLASH        = 0,    // Pantalla de inicio
    DASHBOARD     = 1,    // Dashboard principal con datos PDI
    STATUS_DETAIL = 2,    // Detalle de estado y velocidad
    PRODUCTION    = 3,    // Contadores de producción
    MENU_MAIN     = 4,    // Menú principal
    MENU_MODE     = 5,    // Selector de modo (OFF/ONLINE/SIM)
    MENU_WIFI     = 6,    // Info/acción WiFi
    MENU_OTA      = 7,    // Info OTA
    OTA_PROGRESS  = 8,    // Barra de progreso OTA
    AUDIO_STATUS  = 9,    // Estado de grabación de audio
    VOLUME        = 10    // Control de volumen
};

/** Callback para acciones del menú */
using MenuActionCallback = std::function<void(uint8_t actionId)>;

// IDs de acciones del menú
static constexpr uint8_t ACTION_SET_MODE_OFF   = 1;
static constexpr uint8_t ACTION_SET_MODE_ONLINE = 2;
static constexpr uint8_t ACTION_SET_MODE_SIM   = 3;
static constexpr uint8_t ACTION_START_PORTAL   = 4;
static constexpr uint8_t ACTION_FORCE_OTA      = 5;
static constexpr uint8_t ACTION_RESET_CONFIG   = 6;
static constexpr uint8_t ACTION_VOLUME_CHANGED = 7;

class DisplayMenu {
public:
    /**
     * @param sdaPin  Pin SDA del I2C
     * @param sclPin  Pin SCL del I2C
     */
    DisplayMenu(uint8_t sdaPin = 21, uint8_t sclPin = 22);

    /** Inicializa el display OLED */
    void begin();

    /**
     * Procesa un evento del encoder y actualiza la pantalla.
     * @param event  Evento del encoder rotativo
     */
    void handleInput(EncoderEvent event);

    /**
     * Actualiza el contenido de la pantalla con datos frescos.
     * Llamar periódicamente (~10 Hz).
     */
    void update(const PDIData& data);

    /** Muestra la pantalla de splash inicial */
    void showSplash(const char* version);

    /** Muestra progreso de OTA en pantalla */
    void showOTAProgress(uint8_t percent, const char* status);

    /** Muestra estado del audio (grabando/enviando) */
    void showAudioStatus(bool recording, uint32_t durationMs);

    /** Muestra un mensaje temporal en pantalla */
    void showMessage(const char* line1, const char* line2 = nullptr,
                     uint32_t durationMs = 2000);

    /** Registra callback para acciones del menú */
    void onAction(MenuActionCallback cb);

    /** Obtiene la página actual */
    DisplayPage getCurrentPage() const;

    /** Establece el nivel de volumen mostrado en la UI (0-100) */
    void setVolumeLevel(uint8_t vol);

    /** Obtiene el nivel de volumen actual de la UI */
    uint8_t getVolumeLevel() const;

private:
    // U8g2 para SH1106 128x64 I2C (cambiar constructor si se usa SSD1306)
    U8G2_SH1106_128X64_NONAME_F_HW_I2C _u8g2;

    DisplayPage _currentPage;
    uint8_t _menuCursor;         // Posición del cursor en el menú
    uint8_t _menuItemCount;      // Cantidad de ítems en el menú actual
    MenuActionCallback _actionCb;

    // Datos PDI cacheados para renderizado
    PDIData _cachedData;

    // Mensaje temporal
    bool _showingMessage;
    uint32_t _messageEndMs;
    String _msgLine1, _msgLine2;

    // Volumen
    uint8_t _volumeLevel;

    // Scroll del banner
    int16_t _scrollOffset;
    uint32_t _lastScrollMs;

    // Control de refresco
    uint32_t _lastRenderMs;
    static constexpr uint32_t RENDER_INTERVAL = 100; // 10 FPS

    // ─── Renderizado de páginas ───
    void renderSplash(const char* version);
    void renderDashboard();
    void renderStatusDetail();
    void renderProduction();
    void renderMainMenu();
    void renderModeMenu();
    void renderWifiInfo();
    void renderOtaInfo();
    void renderVolume();
    void renderMessage();

    // ─── Utilidades de dibujo ───
    void drawHeader(const char* title);
    void drawStatusBar();
    void drawProgressBar(uint8_t x, uint8_t y, uint8_t w, uint8_t h,
                         uint8_t percent);
    void drawMenuItem(uint8_t y, const char* label, bool selected);

    /** Ítems del menú principal */
    static constexpr uint8_t MAIN_MENU_ITEMS = 6;
    static const char* mainMenuLabels[];
};

#endif // DISPLAY_MENU_H
