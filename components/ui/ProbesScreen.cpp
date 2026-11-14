
#include <stdio.h>

#include <ProbesScreen.h>
#include <SSD1306I2C.h>
#include <iocfg.h>

ProbesScreen::ProbesScreen(SSD1306I2C& display, uint32_t timeout_tick, MenuCtx& ctx)
    : Screen(display, timeout_tick), m_ctx(ctx)
{
}

void ProbesScreen::open()
{
	m_first = 0;
	Screen::open();
}

void ProbesScreen::update()
{
	Screen::update();
	if (m_closed)
		return;
	if (m_update_count == 0 || m_refresh)
	{
		m_update_count = 100;  // redraw every second for live readings
		m_refresh      = false;
		draw();
	}
	else
	{
		m_update_count--;
	}
}

void ProbesScreen::draw()
{
	std::vector<MenuCtx::ProbeSummary> probes;
	if (m_ctx.probes.list)
		probes = m_ctx.probes.list();
	int n = (int)probes.size();
	if (m_first > n - ROWS)
		m_first = n > ROWS ? n - ROWS : 0;

	char buf[24];
	m_display.clear();
	m_display.setColor(WHITE);
	m_display.setFont(ArialMT_Plain_10);
	snprintf(buf, sizeof(buf), "Soil probes (%d)", n);
	m_display.drawString(0, 0, buf);
	m_display.drawHorizontalLine(0, 12, 128);

	if (n == 0)
	{
		m_display.drawString(0, 16, "None configured.");
		m_display.drawString(0, 28, "Add probes on the");
		m_display.drawString(0, 40, "web page /probes");
	}

	for (int row = 0; row < ROWS && m_first + row < n; row++)
	{
		const MenuCtx::ProbeSummary& p = probes[m_first + row];
		int y = 14 + row * 12;

		snprintf(buf, sizeof(buf), "%d", p.addr);
		m_display.drawString(0, y, buf);
		snprintf(buf, sizeof(buf), "%.9s", p.name);
		m_display.drawString(20, y, buf);

		if (!p.online)
			snprintf(buf, sizeof(buf), "offline");
		else if (p.in_bootloader)
			snprintf(buf, sizeof(buf), "boot");
		else if (!p.calibrated)
			snprintf(buf, sizeof(buf), "no cal");
		else
			snprintf(buf, sizeof(buf), "%.0f%%", p.moisture);
		m_display.drawString(72, y, buf);

		if (p.online && !p.in_bootloader && p.temp_ok)
		{
			snprintf(buf, sizeof(buf), "%.1f", p.temp);
			m_display.drawString(104, y, buf);
		}
	}
	m_display.display();
}

void ProbesScreen::receiveEvent(evt_t* evt)
{
	if (evt->type != EVT_BTN_PRESS && evt->type != EVT_BTN_HOLD_RPT)
		return;
	if (evt->id == BTN_UP_ID)
	{
		if (m_first > 0)
			m_first--;
		m_refresh = true;
	}
	else if (evt->id == BTN_DN_ID)
	{
		m_first++;  // clamped in draw()
		m_refresh = true;
	}
	else if (evt->type == EVT_BTN_PRESS && (evt->id == BTN_BACK_ID || evt->id == BTN_SEL_ID))
	{
		m_closed = true;
	}
	refreshTimeout();
}
