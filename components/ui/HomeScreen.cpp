
#include <ctime>
#include <stdio.h>

#include <HomeScreen.h>

#include <SSD1306I2C.h>
#include <icons.h>

#define CHANNEL_SWITCH_TICKS 300  // 300 x 10ms = 3 seconds

HomeScreen::HomeScreen(SSD1306I2C &display, uint32_t timeout_tick, MenuCtx &menu_ctx)
    : Screen(display, timeout_tick), m_menu_ctx(menu_ctx), m_channel(0), m_channel_ticks(CHANNEL_SWITCH_TICKS)
{
}

HomeScreen::~HomeScreen()
{
}

void HomeScreen::update()
{
	Screen::update();

	// Switch channel every 3 seconds
	if (--m_channel_ticks <= 0)
	{
		m_channel       = 1 - m_channel;
		m_channel_ticks = CHANNEL_SWITCH_TICKS;
		m_update_count  = 0; // force redraw
	}

	if (m_update_count == 0)
	{
		m_update_count = 50;

		m_display.setColor(WHITE);
		m_display.clear();

		m_display.drawBitmap(112, 0, tool_icon16x16, 16, 16);

		uint8_t moisture = m_menu_ctx.moisture.getter ? m_menu_ctx.moisture.getter() : 0;
		m_display.drawBitmap(40, 44, moisture_icon16x16, 16, 16);
		m_display.drawProgressBar(58, 48, 30, 6, moisture);

		update_clock();
		show_wifi();
		show_channel();

		m_display.display();
	}
	else
	{
		m_update_count--;
	}
}

void HomeScreen::receiveEvent(evt_t *evt)
{
}

void HomeScreen::update_clock()
{
	std::time_t now;
	std::time(&now);
	std::tm local_tm;
	std::tm *local = localtime_r(&now, &local_tm);
	m_display.setFont(ArialMT_Plain_10);

	// Show placeholder until NTP has set the clock (year 1970 = not synced)
	if (local->tm_year < 120) // tm_year: years since 1900; 120 = 2020
	{
		m_display.drawString(40, 0, "--:--:--");
		return;
	}

	char buf[9];
	snprintf(buf, sizeof(buf), "%02d:%02d:%02d", local->tm_hour, local->tm_min, local->tm_sec);
	m_display.drawString(40, 0, buf);
}

void HomeScreen::show_wifi()
{
	int8_t rssi = m_menu_ctx.wifi.rssi_getter ? m_menu_ctx.wifi.rssi_getter() : 0;

	m_display.drawBitmap(0, 0, wifi1_icon16x16, 16, 16);

	if (rssi >= 0)
	{
		m_display.drawBitmap(16, -1, cancel_icon16x16, 16, 16);
	}
	else if (rssi > -50)
	{
		m_display.drawBitmap(16, 0, signal4_icon16x16, 16, 16);
	}
	else if (rssi > -65)
	{
		m_display.drawBitmap(16, 0, signal3_icon16x16, 16, 16);
	}
	else if (rssi > -80)
	{
		m_display.drawBitmap(16, 0, signal2_icon16x16, 16, 16);
	}
	else
	{
		m_display.drawBitmap(16, 0, signal1_icon16x16, 16, 16);
	}
}

void HomeScreen::show_channel()
{
	bool     active    = m_menu_ctx.valves.active    ? m_menu_ctx.valves.active(m_channel)    : false;
	uint32_t remaining = m_menu_ctx.valves.remaining ? m_menu_ctx.valves.remaining(m_channel) : 0;

	// Tap icon + channel number, vertically centred in middle band (y=16..47)
	m_display.drawBitmap(2, 24, water_tap_icon16x16, 16, 16);

	m_display.setFont(ArialMT_Plain_16);
	m_display.drawString(20, 24, m_channel == 0 ? "1:" : "2:");

	// On / Off
	m_display.drawString(37, 24, active ? "On" : "Off");

	// Clock icon + remaining time MM:SS (only when active)
	if (active)
	{
		m_display.drawBitmap(65, 24, clock_icon16x16, 16, 16);
		char buf[12];
		snprintf(buf, sizeof(buf), "%02lu:%02lu", remaining / 60, remaining % 60);
		m_display.drawString(83, 24, buf);
	}
}
