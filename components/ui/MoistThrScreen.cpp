
#include <stdio.h>

#include <SSD1306I2C.h>
#include <icons.h>
#include <iocfg.h>

#include <MoistThrScreen.h>

MoistThrScreen::MoistThrScreen(SSD1306I2C& display, uint32_t timeout_tick, MenuCtx& ctx, int ch)
    : Screen(display, timeout_tick),
      m_menu_ctx(ctx),
      m_ch(ch),
      m_blank(false),
      m_val(50)
{
}

MoistThrScreen::~MoistThrScreen()
{
}

void MoistThrScreen::open()
{
	OutputChannel *out = m_menu_ctx.out_ch[m_ch];
	m_val = out ? out->getMoistThreshold() : 50;
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

		OutputChannel *out = m_menu_ctx.out_ch[m_ch];
		char buf[32];
		m_display.setFont(ArialMT_Plain_10);
		snprintf(buf, sizeof(buf), "CH%d skip at %d%%", m_ch + 1, m_val);
		m_display.drawString(0, 0, buf);
		m_display.drawHorizontalLine(0, 12, 128);

		// Live moisture of the zone's probe as the bar fill
		float moisture = m_menu_ctx.probes.ch_moisture ? m_menu_ctx.probes.ch_moisture(m_ch) : -1.0f;
		if (moisture >= 0)
		{
			m_display.drawProgressBar(14, 16, 100, 8, (uint8_t)moisture);
			snprintf(buf, sizeof(buf), "Now %.0f%% (probe %d)", moisture, out ? out->getProbeAddr() : 0);
		}
		else
		{
			m_display.drawRect(14, 16, 101, 9);
			if (out && out->getProbeAddr())
				snprintf(buf, sizeof(buf), "Probe %d: no reading", out->getProbeAddr());
			else
				snprintf(buf, sizeof(buf), "No probe (web page)");
		}

		// Threshold marker (blinks)
		if (!m_blank)
			m_display.drawString(12 + m_val, 25, "^");

		m_display.drawString(0, 38, buf);
		m_display.drawString(0, 52, "Hold SEL to save");
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
			OutputChannel *out = m_menu_ctx.out_ch[m_ch];
			if (out)
			{
				out->setMoistThreshold(m_val);
				out->save();
			}
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
