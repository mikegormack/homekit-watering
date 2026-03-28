
#include <HapResetScreen.h>
#include <SSD1306I2C.h>
#include <iocfg.h>

#include <esp_log.h>

static const char* TAG = "HapResetScreen";

HapResetScreen::HapResetScreen(SSD1306I2C& display, uint32_t timeout_tick, MenuCtx& menu_ctx)
    : Screen(display, timeout_tick), m_menu_ctx(menu_ctx)
{
}

HapResetScreen::~HapResetScreen()
{
}

void HapResetScreen::refreshTimeout()
{
    Screen::refreshTimeout();
    m_refresh = true;
}

void HapResetScreen::update()
{
    Screen::update();

    if (m_update_count > 0)
    {
        m_update_count--;
        return;
    }

    if (m_refresh)
    {
        m_refresh      = false;
        m_update_count = 20;
        draw();
    }
}

void HapResetScreen::receiveEvent(evt_t* evt)
{
    if (evt->id == BTN_SEL_ID && evt->type == EVT_BTN_HOLD)
    {
        ESP_LOGI(TAG, "Resetting HAP pairings");
        if (m_menu_ctx.hap.reset_pairings)
            m_menu_ctx.hap.reset_pairings();
    }
    else if (evt->type == EVT_BTN_PRESS)
    {
        m_closed = true;
    }
}

void HapResetScreen::draw()
{
    m_display.clear();
    m_display.setColor(WHITE);
    m_display.setFont(ArialMT_Plain_10);
    m_display.drawString(4, 2,  "Reset HomeKit");
    m_display.drawString(4, 14, "pairing?");
    m_display.drawString(4, 34, "Hold SEL=Reset");
    m_display.drawString(4, 50, "Any btn=Cancel");
    m_display.display();
}
