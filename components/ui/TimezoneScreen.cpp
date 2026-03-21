
#include <TimezoneScreen.h>
#include <SSD1306I2C.h>
#include <iocfg.h>

struct tz_entry_t
{
	const char *name;
	const char *posix;
};

static constexpr tz_entry_t k_timezones[] = {
	{"UTC",         "UTC0"},
	{"London",      "GMT0BST,M3.5.0/1,M10.5.0"},
	{"Paris",       "CET-1CEST,M3.5.0,M10.5.0/3"},
	{"Moscow",      "MSK-3"},
	{"Dubai",       "<+04>-4"},
	{"India",       "IST-5:30"},
	{"Bangkok",     "<+07>-7"},
	{"Singapore",   "<+08>-8"},
	{"Tokyo",       "JST-9"},
	{"Sydney",      "AEST-10AEDT,M10.1.0,M4.1.0/3"},
	{"Auckland",    "NZST-12NZDT,M9.5.0,M4.1.0/3"},
	{"US Eastern",  "EST5EDT,M3.2.0,M11.1.0"},
	{"US Central",  "CST6CDT,M3.2.0,M11.1.0"},
	{"US Mountain", "MST7MDT,M3.2.0,M11.1.0"},
	{"US Pacific",  "PST8PDT,M3.2.0,M11.1.0"},
	{"US Hawaii",   "HST10"},
};

static constexpr int k_tz_count = sizeof(k_timezones) / sizeof(k_timezones[0]);

const char *timezone_posix(int idx)
{
	if (idx < 0 || idx >= k_tz_count)
		return k_timezones[0].posix;
	return k_timezones[idx].posix;
}

TimezoneScreen::TimezoneScreen(SSD1306I2C &display, uint32_t timeout_tick, MenuCtx &menu_ctx)
    : Screen(display, timeout_tick), m_menu_ctx(menu_ctx)
{
	m_sel = m_menu_ctx.tz_getter ? m_menu_ctx.tz_getter() : 0;
	if (m_sel < 0 || m_sel >= k_tz_count)
		m_sel = 0;
}

TimezoneScreen::~TimezoneScreen()
{
}

void TimezoneScreen::update()
{
	Screen::update();
	if (m_update_count == 0)
	{
		m_update_count = 20;

		int prev = (m_sel > 0) ? (m_sel - 1) : (k_tz_count - 1);
		int next = (m_sel < k_tz_count - 1) ? (m_sel + 1) : 0;

		m_display.clear();

		m_display.setFont(ArialMT_Plain_10);
		m_display.drawString(4, 2, k_timezones[prev].name);

		m_display.drawRect(0, 20, 128, 24);
		m_display.setFont(ArialMT_Plain_16);
		m_display.drawString(4, 24, k_timezones[m_sel].name);

		m_display.setFont(ArialMT_Plain_10);
		m_display.drawString(4, 50, k_timezones[next].name);

		m_display.display();
	}
	else
	{
		m_update_count--;
	}
}

void TimezoneScreen::receiveEvent(evt_t *evt)
{
	if (evt->id == BTN_SEL_ID && evt->type == EVT_BTN_PRESS)
	{
		if (m_menu_ctx.tz_setter)
			m_menu_ctx.tz_setter(m_sel);
		m_closed = true;
	}
	else if (evt->id == BTN_BACK_ID && evt->type == EVT_BTN_PRESS)
	{
		m_closed = true;
	}
	else if (evt->type == EVT_BTN_PRESS || evt->type == EVT_BTN_HOLD_RPT)
	{
		if (evt->id == BTN_UP_ID)
		{
			m_sel = (m_sel > 0) ? (m_sel - 1) : (k_tz_count - 1);
		}
		else if (evt->id == BTN_DN_ID)
		{
			m_sel = (m_sel < k_tz_count - 1) ? (m_sel + 1) : 0;
		}
		m_update_count = 0;
		refreshTimeout();
	}
}
