
#include <InfoScreen.h>
#include <SSD1306I2C.h>

#include <esp_app_format.h>
#include <esp_ota_ops.h>

InfoScreen::InfoScreen(SSD1306I2C& display, uint32_t timeout_tick)
    : Screen(display, timeout_tick)
{
}

void InfoScreen::update()
{
    Screen::update();
    if (m_closed)
        return;

    if (!m_refresh)
        return;
    m_refresh = false;

    const esp_app_desc_t* desc = esp_app_get_description();

    m_display.clear();
    m_display.setColor(WHITE);
    m_display.setFont(ArialMT_Plain_10);
    m_display.drawString(0, 0, "Firmware Info");
    m_display.drawHorizontalLine(0, 12, 128);
    char ver_buf[64];
    snprintf(ver_buf, sizeof(ver_buf), "Version: %s", desc->version);
    m_display.drawString(0, 16, ver_buf);
    m_display.drawString(0, 28, desc->date);
    m_display.drawString(0, 40, desc->time);
    m_display.display();
}

void InfoScreen::receiveEvent(evt_t* evt)
{
    if (evt->type == EVT_BTN_PRESS)
        m_closed = true;
}
