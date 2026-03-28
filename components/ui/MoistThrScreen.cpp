
#include <SSD1306I2C.h>
#include <icons.h>
#include <iocfg.h>

#include <MoistThrScreen.h>

MoistThrScreen::MoistThrScreen(SSD1306I2C& display, uint32_t timeout_tick, MenuCtx& ctx)
    : Screen(display, timeout_tick),
      m_menu_ctx(ctx),
      m_blank(false),
      m_val(ctx.moisture.thr_getter ? ctx.moisture.thr_getter() : 50)
{
}

MoistThrScreen::~MoistThrScreen()
{
}

void MoistThrScreen::open()
{
	m_val = m_menu_ctx.moisture.thr_getter ? m_menu_ctx.moisture.thr_getter() : 50;
	Screen::open();
}

void MoistThrScreen::update()
{
	Screen::update();
	if (m_update_count == 0)
	{
		m_display.clear();
		if (!m_blank)
		{
			m_blank = true;
			m_update_count = 20;
		}
		else
		{
			m_blank = false;
			m_update_count = 25;
		}

		m_display.setFont(ArialMT_Plain_16);
		m_display.drawString(0, 0, "Cur:");

		// Live moisture reading as the bar fill
		uint8_t moisture = m_menu_ctx.moisture.getter ? m_menu_ctx.moisture.getter() : 0;
		m_display.drawProgressBar(14, 16, 100, 8, moisture);

		// Threshold marker (blinks)
		if (!m_blank)
			m_display.drawString(14 + m_val, 26, "^");

		m_display.display();
	}
	else
	{
		m_update_count--;
	}
}

void MoistThrScreen::receiveEvent(evt_t *evt)
{
	if (evt->id == BTN_SEL_ID)
	{
		if (evt->type == EVT_BTN_HOLD)
		{
			if (m_menu_ctx.moisture.thr_setter)
				m_menu_ctx.moisture.thr_setter(m_val);
			m_closed = true;
		}
	}
	else if (evt->id == BTN_BACK_ID && evt->type == EVT_BTN_PRESS)
	{
		m_closed = true;
	}
	else if (evt->type == EVT_BTN_PRESS || evt->type == EVT_BTN_HOLD_RPT)
	{
		if (evt->id == BTN_UP_ID)
		{
			if (m_val < 100)
				m_val++;
		}
		else if (evt->id == BTN_DN_ID)
		{
			if (m_val > 0)
				m_val--;
		}
		m_update_count = 0;
		refreshTimeout();
	}
}
