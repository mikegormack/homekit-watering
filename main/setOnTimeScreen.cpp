
#include <ctime>
#include <sstream>
#include <iomanip>

#include <setOnTimeScreen.h>

#include <SSD1306I2C.h>
#include <icons.h>

#include <esp_log.h>

static const char *TAG = "setOnTimeScreen";

setOnTimeScreen::setOnTimeScreen(SSD1306I2C &display) : screen(display),
														m_sel_field(0)
{
}

setOnTimeScreen::~setOnTimeScreen()
{
}

void setOnTimeScreen::update()
{
	if (m_update_count == 0)
	{
		m_display.clear();
		if (!m_blank)
			m_blank = true;
		else
			m_blank = false;
		m_update_count = 50;

		m_display.setFont(ArialMT_Plain_16);

		m_display.drawString(0, 0, "Chan 1");

		time_val_t tm;
		tm.hour = 22;
		tm.min = 39;
		tm.duration = 25;

		displaySetTime(1, 30, &tm, ((m_sel_field <= 3) && m_blank) ? m_sel_field : 0);

		tm.hour = 6;
		tm.min = 15;
		tm.duration = 45;
		displaySetTime(2, 48, &tm, ((m_sel_field > 3) && m_blank) ? (m_sel_field - 3) : 0);

		m_display.display();
	}
	else
	{
		m_update_count--;
	}
}

void setOnTimeScreen::receiveEvent(evt_t *evt)
{
	if (evt->type == EVT_BTN_PRESS && evt->id == 2)
	{
		m_sel_field++;
		if (m_sel_field == 7)
			m_sel_field = 1;
		ESP_LOGI(TAG, "Sel field %d", m_sel_field);
	}
}

void setOnTimeScreen::displaySetTime(uint8_t id, uint8_t height, time_val_t *tm, uint8_t blank)
{
	std::stringstream ss;
	ss << +id << '.';
	m_display.drawString(0, height, ss.str());
	m_display.drawBitmap(16, height, clock_icon16x16, 16, 16);
	if (blank != 1)
	{
		ss.clear();
		ss.str("");
		ss << std::setw(2) << std::setfill('0') << +tm->hour;
		m_display.drawString(36, height, ss.str());
	}
	m_display.drawString(57, height, ":");
	if (blank != 2)
	{
		ss.clear();
		ss.str("");
		ss << std::setw(2) << std::setfill('0') << +tm->min;
		m_display.drawString(61, height, ss.str());
	}
	m_display.drawBitmap(84, height, timer_icon16x16, 16, 16);
	if (blank != 3)
	{
		ss.clear();
		ss.str("");
		ss << std::setw(2) << std::setfill('0') << +tm->duration;
		m_display.drawString(104, height, ss.str());
	}
}
