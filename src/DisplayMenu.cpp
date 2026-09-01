/**
 * @file DisplayMenu.cpp
 * @brief Implementación del sistema de menú y visualización OLED
 */

#include "DisplayMenu.h"

const char* DisplayMenu::mainMenuLabels[] = {
    "Modo",
    "Volumen",
    "WiFi/Portal",
    "OTA Update",
    "Info Sistema",
    "Reset Config"
};

DisplayMenu::DisplayMenu(uint8_t sdaPin, uint8_t sclPin)
    : _u8g2(U8G2_R2, /* reset= */ U8X8_PIN_NONE, sclPin, sdaPin)
    , _currentPage(DisplayPage::SPLASH)
    , _menuCursor(0)
    , _menuItemCount(0)
    , _actionCb(nullptr)
    , _showingMessage(false)
    , _messageEndMs(0)
    , _volumeLevel(80)
    , _scrollOffset(0)
    , _lastScrollMs(0)
    , _lastRenderMs(0)
{
    memset(&_cachedData, 0, sizeof(_cachedData));
}

void DisplayMenu::begin() {
    _u8g2.begin();
    _u8g2.setFont(u8g2_font_6x10_tr);
    _u8g2.setContrast(200);
    Serial.println("[Display] OLED inicializado");
}

// ─────────────────────────────────────────────────────────────────────────────
// Manejo de entrada del encoder
// ─────────────────────────────────────────────────────────────────────────────

void DisplayMenu::handleInput(EncoderEvent event) {
    if (event == EncoderEvent::NONE) return;

    switch (_currentPage) {
        case DisplayPage::DASHBOARD:
        case DisplayPage::STATUS_DETAIL:
        case DisplayPage::PRODUCTION: {
            // En las pantallas de datos, el encoder cambia de página
            if (event == EncoderEvent::ROTATE_CW) {
                uint8_t p = static_cast<uint8_t>(_currentPage);
                if (p < static_cast<uint8_t>(DisplayPage::PRODUCTION)) {
                    _currentPage = static_cast<DisplayPage>(p + 1);
                }
            } else if (event == EncoderEvent::ROTATE_CCW) {
                uint8_t p = static_cast<uint8_t>(_currentPage);
                if (p > static_cast<uint8_t>(DisplayPage::DASHBOARD)) {
                    _currentPage = static_cast<DisplayPage>(p - 1);
                }
            } else if (event == EncoderEvent::BUTTON_PRESS) {
                // Entrar al menú principal
                _currentPage = DisplayPage::MENU_MAIN;
                _menuCursor = 0;
                _menuItemCount = MAIN_MENU_ITEMS;
            } else if (event == EncoderEvent::BUTTON_LONG) {
                // Pulsación larga: abrir portal WiFi directo
                if (_actionCb) _actionCb(ACTION_START_PORTAL);
            }
            break;
        }

        case DisplayPage::MENU_MAIN: {
            if (event == EncoderEvent::ROTATE_CW) {
                if (_menuCursor < _menuItemCount - 1) _menuCursor++;
            } else if (event == EncoderEvent::ROTATE_CCW) {
                if (_menuCursor > 0) _menuCursor--;
            } else if (event == EncoderEvent::BUTTON_PRESS) {
                // Seleccionar ítem del menú
                switch (_menuCursor) {
                    case 0: // Modo
                        _currentPage = DisplayPage::MENU_MODE;
                        _menuCursor = static_cast<uint8_t>(
                            _cachedData.deviceMode);
                        _menuItemCount = 3;
                        break;
                    case 1: // Volumen
                        _currentPage = DisplayPage::VOLUME;
                        break;
                    case 2: // WiFi/Portal
                        _currentPage = DisplayPage::MENU_WIFI;
                        break;
                    case 3: // OTA
                        _currentPage = DisplayPage::MENU_OTA;
                        break;
                    case 4: // Info Sistema
                        showMessage("Gemelo Digital", "ESP32-S3 v1.0", 3000);
                        break;
                    case 5: // Reset Config
                        if (_actionCb) _actionCb(ACTION_RESET_CONFIG);
                        showMessage("Config", "Reseteada!", 2000);
                        break;
                }
            } else if (event == EncoderEvent::BUTTON_LONG) {
                // Volver al dashboard
                _currentPage = DisplayPage::DASHBOARD;
            }
            break;
        }

        case DisplayPage::MENU_MODE: {
            if (event == EncoderEvent::ROTATE_CW) {
                if (_menuCursor < 2) _menuCursor++;
            } else if (event == EncoderEvent::ROTATE_CCW) {
                if (_menuCursor > 0) _menuCursor--;
            } else if (event == EncoderEvent::BUTTON_PRESS) {
                // Aplicar modo seleccionado
                switch (_menuCursor) {
                    case 0: if (_actionCb) _actionCb(ACTION_SET_MODE_OFF); break;
                    case 1: if (_actionCb) _actionCb(ACTION_SET_MODE_ONLINE); break;
                    case 2: if (_actionCb) _actionCb(ACTION_SET_MODE_SIM); break;
                }
                _currentPage = DisplayPage::DASHBOARD;
            } else if (event == EncoderEvent::BUTTON_LONG) {
                _currentPage = DisplayPage::MENU_MAIN;
                _menuCursor = 0;
                _menuItemCount = MAIN_MENU_ITEMS;
            }
            break;
        }

        case DisplayPage::VOLUME: {
            if (event == EncoderEvent::ROTATE_CW) {
                if (_volumeLevel <= 95) _volumeLevel += 5;
                else _volumeLevel = 100;
                if (_actionCb) _actionCb(ACTION_VOLUME_CHANGED);
            } else if (event == EncoderEvent::ROTATE_CCW) {
                if (_volumeLevel >= 5) _volumeLevel -= 5;
                else _volumeLevel = 0;
                if (_actionCb) _actionCb(ACTION_VOLUME_CHANGED);
            } else if (event == EncoderEvent::BUTTON_PRESS ||
                       event == EncoderEvent::BUTTON_LONG) {
                _currentPage = DisplayPage::MENU_MAIN;
                _menuCursor = 1;
                _menuItemCount = MAIN_MENU_ITEMS;
            }
            break;
        }

        case DisplayPage::MENU_WIFI: {
            if (event == EncoderEvent::BUTTON_PRESS) {
                if (_actionCb) _actionCb(ACTION_START_PORTAL);
                showMessage("Portal WiFi", "Iniciado!", 2000);
            } else if (event == EncoderEvent::BUTTON_LONG) {
                _currentPage = DisplayPage::MENU_MAIN;
                _menuCursor = 2;
                _menuItemCount = MAIN_MENU_ITEMS;
            }
            break;
        }

        case DisplayPage::MENU_OTA: {
            if (event == EncoderEvent::BUTTON_PRESS) {
                if (_actionCb) _actionCb(ACTION_FORCE_OTA);
            } else if (event == EncoderEvent::BUTTON_LONG) {
                _currentPage = DisplayPage::MENU_MAIN;
                _menuCursor = 3;
                _menuItemCount = MAIN_MENU_ITEMS;
            }
            break;
        }

        default:
            // En otras pantallas, cualquier botón vuelve al dashboard
            if (event == EncoderEvent::BUTTON_PRESS ||
                event == EncoderEvent::BUTTON_LONG) {
                _currentPage = DisplayPage::DASHBOARD;
            }
            break;
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// Actualización periódica del display
// ─────────────────────────────────────────────────────────────────────────────

void DisplayMenu::update(const PDIData& data) {
    uint32_t now = millis();
    if (now - _lastRenderMs < RENDER_INTERVAL) return;
    _lastRenderMs = now;

    _cachedData = data;

    // Verificar si hay un mensaje temporal activo
    if (_showingMessage && now < _messageEndMs) {
        _u8g2.clearBuffer();
        renderMessage();
        _u8g2.sendBuffer();
        return;
    }
    _showingMessage = false;

    _u8g2.clearBuffer();

    switch (_currentPage) {
        case DisplayPage::SPLASH:
            renderSplash("v1.0.0");
            break;
        case DisplayPage::DASHBOARD:
            renderDashboard();
            break;
        case DisplayPage::STATUS_DETAIL:
            renderStatusDetail();
            break;
        case DisplayPage::PRODUCTION:
            renderProduction();
            break;
        case DisplayPage::MENU_MAIN:
            renderMainMenu();
            break;
        case DisplayPage::MENU_MODE:
            renderModeMenu();
            break;
        case DisplayPage::MENU_WIFI:
            renderWifiInfo();
            break;
        case DisplayPage::MENU_OTA:
            renderOtaInfo();
            break;
        case DisplayPage::VOLUME:
            renderVolume();
            break;
        default:
            renderDashboard();
            break;
    }

    _u8g2.sendBuffer();
}

// ─────────────────────────────────────────────────────────────────────────────
// Pantallas especiales
// ─────────────────────────────────────────────────────────────────────────────

void DisplayMenu::showSplash(const char* version) {
    _currentPage = DisplayPage::SPLASH;
    _u8g2.clearBuffer();
    renderSplash(version);
    _u8g2.sendBuffer();
}

void DisplayMenu::showOTAProgress(uint8_t percent, const char* status) {
    _currentPage = DisplayPage::OTA_PROGRESS;
    _u8g2.clearBuffer();

    drawHeader("OTA UPDATE");

    _u8g2.setFont(u8g2_font_6x10_tr);
    _u8g2.drawStr(0, 28, status);

    drawProgressBar(4, 35, 120, 12, percent);

    char buf[16];
    snprintf(buf, sizeof(buf), "%d%%", percent);
    _u8g2.drawStr(52, 60, buf);

    _u8g2.sendBuffer();
}

void DisplayMenu::showAudioStatus(bool recording, uint32_t durationMs) {
    _u8g2.clearBuffer();
    drawHeader("AUDIO");

    _u8g2.setFont(u8g2_font_6x10_tr);

    if (recording) {
        // Indicador de grabación parpadeante
        if ((millis() / 500) % 2) {
            _u8g2.drawDisc(10, 30, 5);
        }
        _u8g2.drawStr(20, 33, "GRABANDO...");

        char buf[24];
        snprintf(buf, sizeof(buf), "%.1f / 20.0 seg", durationMs / 1000.0f);
        _u8g2.drawStr(10, 48, buf);

        drawProgressBar(4, 52, 120, 8, (uint8_t)(durationMs * 100 / 20000));
    } else {
        _u8g2.drawStr(20, 33, "Reproduciendo...");

        char volBuf[16];
        snprintf(volBuf, sizeof(volBuf), "Vol: %d%%", _volumeLevel);
        _u8g2.drawStr(20, 48, volBuf);
        drawProgressBar(20, 52, 88, 6, _volumeLevel);
    }

    _u8g2.sendBuffer();
}

void DisplayMenu::setVolumeLevel(uint8_t vol) {
    _volumeLevel = vol > 100 ? 100 : vol;
}

uint8_t DisplayMenu::getVolumeLevel() const {
    return _volumeLevel;
}

void DisplayMenu::showMessage(const char* line1, const char* line2,
                               uint32_t durationMs) {
    _showingMessage = true;
    _messageEndMs = millis() + durationMs;
    _msgLine1 = line1;
    _msgLine2 = line2 ? line2 : "";
}

void DisplayMenu::onAction(MenuActionCallback cb) {
    _actionCb = cb;
}

DisplayPage DisplayMenu::getCurrentPage() const {
    return _currentPage;
}

// ─────────────────────────────────────────────────────────────────────────────
// Renderizado de páginas individuales
// ─────────────────────────────────────────────────────────────────────────────

void DisplayMenu::renderSplash(const char* version) {
    _u8g2.setFont(u8g2_font_helvB12_tr);
    _u8g2.drawStr(8, 20, "GEMELO");
    _u8g2.drawStr(8, 38, "DIGITAL");

    _u8g2.setFont(u8g2_font_6x10_tr);
    _u8g2.drawStr(8, 55, version);
    _u8g2.drawStr(60, 55, "ESP32-S3");

    _u8g2.drawFrame(0, 0, 128, 64);
}

void DisplayMenu::renderDashboard() {
    drawHeader(machineStateToStr(_cachedData.status.stateCurrent));

    _u8g2.setFont(u8g2_font_6x10_tr);

    // Velocidad actual (grande)
    char speedBuf[16];
    snprintf(speedBuf, sizeof(speedBuf), "%.0f", _cachedData.status.curMachSpeed);
    _u8g2.setFont(u8g2_font_helvB18_tn);
    _u8g2.drawStr(5, 40, speedBuf);

    _u8g2.setFont(u8g2_font_6x10_tr);
    _u8g2.drawStr(70, 28, "u/min");

    // Modo y calidad
    char modeBuf[32];
    snprintf(modeBuf, sizeof(modeBuf), "%s | %s",
             deviceModeToStr(_cachedData.deviceMode),
             unitModeToStr(_cachedData.status.unitModeCurrent));
    _u8g2.drawStr(0, 55, modeBuf);

    // Indicador de conexión
    if (_cachedData.connectionStatus) {
        _u8g2.drawDisc(122, 55, 3);
    } else {
        _u8g2.drawCircle(122, 55, 3);
    }

    // Barra de velocidad visual
    uint8_t speedPercent = 0;
    if (_cachedData.status.machSpeed > 0) {
        speedPercent = (uint8_t)(_cachedData.status.curMachSpeed * 100.0f
                                 / _cachedData.status.machSpeed);
    }
    drawProgressBar(0, 44, 128, 4, min((uint8_t)100, speedPercent));
}

void DisplayMenu::renderStatusDetail() {
    drawHeader("ESTADO DETALLE");

    _u8g2.setFont(u8g2_font_6x10_tr);

    char buf[32];

    snprintf(buf, sizeof(buf), "Estado: %s",
             machineStateToStr(_cachedData.status.stateCurrent));
    _u8g2.drawStr(0, 22, buf);

    snprintf(buf, sizeof(buf), "Previo: %s",
             machineStateToStr(_cachedData.status.statePrevious));
    _u8g2.drawStr(0, 33, buf);

    snprintf(buf, sizeof(buf), "Duracion: %ds", _cachedData.status.stateDuration);
    _u8g2.drawStr(0, 44, buf);

    snprintf(buf, sizeof(buf), "Vel: %.1f/%.1f",
             _cachedData.status.curMachSpeed,
             _cachedData.status.machSpeed);
    _u8g2.drawStr(0, 55, buf);
}

void DisplayMenu::renderProduction() {
    drawHeader("PRODUCCION");

    _u8g2.setFont(u8g2_font_6x10_tr);

    char buf[32];

    snprintf(buf, sizeof(buf), "Total:  %lu",
             (unsigned long)_cachedData.admin.prodProcessedCount);
    _u8g2.drawStr(0, 22, buf);

    snprintf(buf, sizeof(buf), "Buenos: %lu",
             (unsigned long)_cachedData.goodCount());
    _u8g2.drawStr(0, 33, buf);

    snprintf(buf, sizeof(buf), "Defect: %lu",
             (unsigned long)_cachedData.admin.prodDefectiveCount);
    _u8g2.drawStr(0, 44, buf);

    snprintf(buf, sizeof(buf), "Calidad: %s",
             qualitySignalToStr(_cachedData.qualitySignal));
    _u8g2.drawStr(0, 55, buf);
}

void DisplayMenu::renderMainMenu() {
    drawHeader("MENU");

    _u8g2.setFont(u8g2_font_6x10_tr);

    // Calcular ventana visible (máximo 4 ítems visibles)
    uint8_t startIdx = 0;
    if (_menuCursor > 3) startIdx = _menuCursor - 3;

    for (uint8_t i = 0; i < 4 && (startIdx + i) < MAIN_MENU_ITEMS; i++) {
        uint8_t idx = startIdx + i;
        uint8_t y = 22 + i * 11;
        drawMenuItem(y, mainMenuLabels[idx], idx == _menuCursor);
    }
}

void DisplayMenu::renderModeMenu() {
    drawHeader("MODO OPERACION");

    _u8g2.setFont(u8g2_font_6x10_tr);

    const char* modeLabels[] = {"OFF (Offline)", "ONLINE (Broker)", "SIMULADOR"};

    for (uint8_t i = 0; i < 3; i++) {
        drawMenuItem(22 + i * 14, modeLabels[i], i == _menuCursor);
    }
}

void DisplayMenu::renderWifiInfo() {
    drawHeader("WIFI / PORTAL");

    _u8g2.setFont(u8g2_font_6x10_tr);

    if (WiFi.status() == WL_CONNECTED) {
        _u8g2.drawStr(0, 22, "Estado: Conectado");
        char buf[32];
        snprintf(buf, sizeof(buf), "IP: %s", WiFi.localIP().toString().c_str());
        _u8g2.drawStr(0, 33, buf);
        snprintf(buf, sizeof(buf), "RSSI: %d dBm", WiFi.RSSI());
        _u8g2.drawStr(0, 44, buf);
    } else {
        _u8g2.drawStr(0, 22, "Estado: Desconectado");
    }

    _u8g2.drawStr(0, 58, "[OK] Abrir Portal");
}

void DisplayMenu::renderOtaInfo() {
    drawHeader("OTA UPDATE");

    _u8g2.setFont(u8g2_font_6x10_tr);
    _u8g2.drawStr(0, 22, "Servidor OTA:");
    _u8g2.drawStr(0, 33, "Configurado en NVS");
    _u8g2.drawStr(0, 50, "[OK] Forzar Update");
}

void DisplayMenu::renderVolume() {
    drawHeader("VOLUMEN");

    // Número grande con el porcentaje
    char buf[8];
    snprintf(buf, sizeof(buf), "%d%%", _volumeLevel);
    _u8g2.setFont(u8g2_font_helvB18_tr);
    uint8_t w = _u8g2.getStrWidth(buf);
    _u8g2.drawStr((128 - w) / 2, 38, buf);

    // Icono de speaker (triángulo + ondas)
    _u8g2.setFont(u8g2_font_6x10_tr);
    if (_volumeLevel == 0) {
        _u8g2.drawStr(4, 38, "x");
    }

    // Barra de volumen
    drawProgressBar(4, 46, 120, 8, _volumeLevel);

    // Instrucciones
    _u8g2.setFont(u8g2_font_5x7_tr);
    _u8g2.drawStr(12, 62, "Girar:ajustar  OK:volver");
}

void DisplayMenu::renderMessage() {
    _u8g2.setFont(u8g2_font_helvB12_tr);

    uint8_t w1 = _u8g2.getStrWidth(_msgLine1.c_str());
    _u8g2.drawStr((128 - w1) / 2, 28, _msgLine1.c_str());

    if (_msgLine2.length() > 0) {
        _u8g2.setFont(u8g2_font_6x10_tr);
        uint8_t w2 = _u8g2.getStrWidth(_msgLine2.c_str());
        _u8g2.drawStr((128 - w2) / 2, 48, _msgLine2.c_str());
    }

    _u8g2.drawFrame(0, 0, 128, 64);
}

// ─────────────────────────────────────────────────────────────────────────────
// Utilidades de dibujo
// ─────────────────────────────────────────────────────────────────────────────

void DisplayMenu::drawHeader(const char* title) {
    _u8g2.setFont(u8g2_font_6x10_tr);
    _u8g2.drawStr(0, 10, title);
    _u8g2.drawHLine(0, 12, 128);
}

void DisplayMenu::drawStatusBar() {
    _u8g2.drawHLine(0, 54, 128);
}

void DisplayMenu::drawProgressBar(uint8_t x, uint8_t y, uint8_t w, uint8_t h,
                                    uint8_t percent) {
    _u8g2.drawFrame(x, y, w, h);
    uint8_t fillW = (uint8_t)((uint16_t)(w - 2) * percent / 100);
    if (fillW > 0) {
        _u8g2.drawBox(x + 1, y + 1, fillW, h - 2);
    }
}

void DisplayMenu::drawMenuItem(uint8_t y, const char* label, bool selected) {
    if (selected) {
        _u8g2.drawBox(0, y - 9, 128, 11);
        _u8g2.setDrawColor(0);
        _u8g2.drawStr(4, y, label);
        _u8g2.setDrawColor(1);
    } else {
        _u8g2.drawStr(4, y, label);
    }
}
