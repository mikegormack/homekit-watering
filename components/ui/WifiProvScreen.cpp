
#include <SSD1306I2C.h>
#include <icons.h>
#include <iocfg.h>

#include <qrcodegen.h>

#include <esp_log.h>

#include <WifiProvScreen.h>

static const char* TAG = "WifiProvScreen";

// Show "Connected!" for ~3 seconds (300 x 10ms updates) before closing
static constexpr int SUCCESS_DISPLAY_TICKS = 300;

WifiProvScreen::WifiProvScreen(SSD1306I2C& display, MenuCtx& menu_ctx)
    : Screen(display, 0 /* no timeout */),
      m_menu_ctx(menu_ctx),
      m_qr_code(nullptr),
      m_state(PROV_STATE_STARTING),
      m_success_count(0)
{
}

WifiProvScreen::~WifiProvScreen()
{
}

void WifiProvScreen::refreshTimeout()
{
    Screen::refreshTimeout();

    // Start provisioning the first time this screen becomes active
    if (m_state == PROV_STATE_STARTING)
    {
        ESP_LOGI(TAG, "Starting provisioning");
        if (m_menu_ctx.start_prov)
        {
            m_qr_code = m_menu_ctx.start_prov();
        }
        m_state   = m_qr_code ? PROV_STATE_QR : PROV_STATE_ERROR;
        m_refresh = true;
    }
}

void WifiProvScreen::update()
{
    // No base timeout — manage our own lifecycle
    if (m_update_count > 0)
    {
        m_update_count--;
        return;
    }

    if (m_state == PROV_STATE_QR)
    {
        // Poll for connection
        if (m_menu_ctx.is_connected && m_menu_ctx.is_connected())
        {
            ESP_LOGI(TAG, "WiFi connected — provisioning complete");
            m_state         = PROV_STATE_SUCCESS;
            m_success_count = SUCCESS_DISPLAY_TICKS;
            m_refresh       = true;
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
            case PROV_STATE_STARTING:
                drawStarting();
                break;
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
    if (evt->id == BTN_BACK_ID && evt->type == EVT_BTN_PRESS)
    {
        cancel();
    }
}

void WifiProvScreen::cancel()
{
    ESP_LOGI(TAG, "Provisioning cancelled");
    if (m_menu_ctx.stop_prov)
    {
        m_menu_ctx.stop_prov();
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

    // Draw QR code 1px per module, centred vertically on the left 64px
    int x_off = 2;
    int y_off = (64 - size) / 2;
    if (y_off < 0) y_off = 0;

    for (int y = 0; y < size && (y + y_off) < 64; y++)
    {
        for (int x = 0; x < size && (x + x_off) < 64; x++)
        {
            if (qrcodegen_getModule(m_qr_code.get(), x, y))
            {
                m_display.setPixel(x + x_off, y + y_off);
            }
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

void WifiProvScreen::drawStarting()
{
    m_display.clear();
    m_display.setColor(WHITE);
    m_display.setFont(ArialMT_Plain_16);
    m_display.drawString(4, 20, "Starting...");
    m_display.display();
}
