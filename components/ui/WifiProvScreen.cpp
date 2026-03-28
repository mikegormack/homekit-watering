
#include <SSD1306I2C.h>
#include <icons.h>
#include <iocfg.h>

#include <qrcodegen.h>

#include <esp_log.h>

#include <WifiProvScreen.h>

static const char* TAG = "WifiProvScreen";

// Show "Connected!" for ~3 seconds (300 x 10ms updates) before closing
static constexpr int SUCCESS_DISPLAY_TICKS = 300;

WifiProvScreen::WifiProvScreen(SSD1306I2C& display, MenuCtx& menu_ctx, uint32_t timeout_tick)
    : Screen(display, timeout_tick),
      m_menu_ctx(menu_ctx),
      m_qr_code(nullptr),
      m_state(PROV_STATE_ERROR),
      m_success_count(0)
{
}

WifiProvScreen::~WifiProvScreen()
{
}

void WifiProvScreen::refreshTimeout()
{
    Screen::refreshTimeout();

    // (Re)start provisioning every time this screen becomes active
    ESP_LOGI(TAG, "Starting provisioning");
    m_qr_code       = nullptr;
    m_success_count = 0;
    m_update_count  = 0;

    if (m_menu_ctx.wifi.start_prov)
        m_qr_code = m_menu_ctx.wifi.start_prov();

    m_state   = m_qr_code ? PROV_STATE_QR : PROV_STATE_ERROR;
    m_refresh = true;
}

void WifiProvScreen::update()
{
    Screen::update(); // handles timeout → m_closed
    if (m_closed)
    {
        cancel();
        return;
    }

    if (m_update_count > 0)
    {
        m_update_count--;
        return;
    }

    if (m_state == PROV_STATE_QR)
    {
        if (m_menu_ctx.wifi.prov_status)
        {
            auto status = m_menu_ctx.wifi.prov_status();
            if (status == MenuCtx::ProvStatus::Connected)
            {
                ESP_LOGI(TAG, "WiFi connected — provisioning complete");
                m_state         = PROV_STATE_SUCCESS;
                m_success_count = SUCCESS_DISPLAY_TICKS;
                m_refresh       = true;
            }
            else if (status == MenuCtx::ProvStatus::Failed)
            {
                ESP_LOGE(TAG, "Provisioning credentials failed");
                m_state   = PROV_STATE_ERROR;
                m_refresh = true;
            }
        }
        m_update_count = 20; // re-poll every 200ms
    }
    else if (m_state == PROV_STATE_SUCCESS)
    {
        m_success_count--;
        if (m_success_count <= 0)
        {
            m_closed = true;
            return;
        }
        m_update_count = 10;
    }

    if (m_refresh)
    {
        m_refresh = false;
        switch (m_state)
        {
            case PROV_STATE_QR:
                drawQR();
                break;
            case PROV_STATE_SUCCESS:
                drawSuccess();
                break;
            case PROV_STATE_ERROR:
                drawError();
                break;
        }
    }
}

void WifiProvScreen::receiveEvent(evt_t* evt)
{
    if (evt->type != EVT_BTN_PRESS)
        return;

    if (m_state == PROV_STATE_QR)
    {
        if (evt->id == BTN_BACK_ID)
            cancel();
    }
    else
    {
        // Success or error — any button exits back to menu
        m_closed = true;
    }
}

void WifiProvScreen::cancel()
{
    ESP_LOGI(TAG, "Provisioning cancelled");
    if (m_menu_ctx.wifi.stop_prov)
    {
        m_menu_ctx.wifi.stop_prov();
    }
    m_closed = true;
}

void WifiProvScreen::drawQR()
{
    m_display.clear();
    m_display.setColor(WHITE);

    if (!m_qr_code)
    {
        drawError();
        return;
    }

    int size = qrcodegen_getSize(m_qr_code.get());

    // Scale to fill 64x64 using nearest-neighbor (version 4 = 33 modules → ~1.94px/module)
    for (int py = 0; py < 64; py++)
    {
        for (int px = 0; px < 64; px++)
        {
            if (qrcodegen_getModule(m_qr_code.get(), (px * size) / 64, (py * size) / 64))
                m_display.setPixel(px, py);
        }
    }

    // Right side: instructions
    m_display.setFont(ArialMT_Plain_10);
    m_display.drawString(66, 4,  "Scan QR");
    m_display.drawString(66, 18, "in ESP");
    m_display.drawString(66, 30, "Prov app");
    m_display.drawBitmap(66, 46, cancel_icon16x16, 16, 16);
    m_display.drawString(84, 50, "=Cancel");

    m_display.display();
}

void WifiProvScreen::drawSuccess()
{
    m_display.clear();
    m_display.setColor(WHITE);
    m_display.setFont(ArialMT_Plain_16);
    m_display.drawString(8, 10, "WiFi");
    m_display.drawString(8, 30, "Connected!");
    m_display.display();
}

void WifiProvScreen::drawError()
{
    m_display.clear();
    m_display.setColor(WHITE);
    m_display.setFont(ArialMT_Plain_16);
    m_display.drawString(4, 8,  "Prov");
    m_display.drawString(4, 28, "Failed");
    m_display.setFont(ArialMT_Plain_10);
    m_display.drawString(4, 50, "BACK to exit");
    m_display.display();
}

