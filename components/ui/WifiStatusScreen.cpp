
#include <SSD1306I2C.h>
#include <icons.h>
#include <iocfg.h>

#include <esp_log.h>

#include <WifiStatusScreen.h>

WifiStatusScreen::WifiStatusScreen(SSD1306I2C& display, uint32_t timeout_tick, MenuCtx& menu_ctx)
    : Screen(display, timeout_tick), m_menu_ctx(menu_ctx)
{
}

WifiStatusScreen::~WifiStatusScreen()
{
}

void WifiStatusScreen::update()
{
    Screen::update();
    if (m_closed)
        return;

    if (m_update_count > 0)
    {
        m_update_count--;
        return;
    }
    m_update_count = 100; // refresh every second

    m_display.clear();
    m_display.setColor(WHITE);

    if (m_menu_ctx.wifi_info)
    {
        auto info = m_menu_ctx.wifi_info();

        m_display.setFont(ArialMT_Plain_10);
        m_display.drawString(0, 0, "WiFi Status");
        m_display.drawHorizontalLine(0, 12, 128);

        if (info.connected)
        {
            m_display.drawBitmap(0, 16, wifi1_icon16x16, 16, 16);
            m_display.drawString(20, 18, info.ssid);
            m_display.drawString(0, 36, "IP:");
            m_display.drawString(20, 36, info.ip);
        }
        else
        {
            m_display.drawBitmap(0, 16, cancel_icon16x16, 16, 16);
            m_display.drawString(20, 22, "Not connected");
        }
    }

    m_display.display();
}

void WifiStatusScreen::receiveEvent(evt_t* evt)
{
    if (evt->type == EVT_BTN_PRESS)
        m_closed = true;
}
