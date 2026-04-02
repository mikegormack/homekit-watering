
#include <SchedEnableScreen.h>
#include <SSD1306I2C.h>
#include <iocfg.h>

SchedEnableScreen::SchedEnableScreen(SSD1306I2C &display, uint32_t timeout_tick, OutputChannel &ch)
    : Screen(display, timeout_tick),
      m_ch(ch),
      m_enabled(ch.isSchedEnabled())
{
}

SchedEnableScreen::~SchedEnableScreen()
{
}

void SchedEnableScreen::open()
{
    m_enabled = m_ch.isSchedEnabled();
    Screen::open();
}

void SchedEnableScreen::update()
{
    Screen::update();
    if (m_update_count == 0)
    {
        m_update_count = 20;
        m_display.clear();
        m_display.setFont(ArialMT_Plain_16);
        m_display.drawString(0, 0, "Schedule");
        m_display.drawString(0, 24, m_enabled ? "ENABLED" : "DISABLED");
        m_display.setFont(ArialMT_Plain_10);
        m_display.drawString(0, 52, "Hold SEL to save");
        m_display.display();
    }
    else
    {
        m_update_count--;
    }
}

void SchedEnableScreen::receiveEvent(evt_t *evt)
{
    if (evt->id == BTN_SEL_ID && evt->type == EVT_BTN_HOLD)
    {
        m_ch.setSchedEnabled(m_enabled);
        m_ch.save();
        m_closed = true;
    }
    else if (evt->id == BTN_BACK_ID && evt->type == EVT_BTN_PRESS)
    {
        m_closed = true;
    }
    else if (evt->type == EVT_BTN_PRESS && (evt->id == BTN_UP_ID || evt->id == BTN_DN_ID ||
                                             evt->id == BTN_SEL_ID))
    {
        m_enabled = !m_enabled;
        m_update_count = 0;
        refreshTimeout();
    }
}
