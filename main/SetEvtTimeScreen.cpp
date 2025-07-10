
#include <ctime>
#include <sstream>
#include <iomanip>

#include <SetEvtTimeScreen.h>

#include <SSD1306I2C.h>
#include <icons.h>
#include <iocfg.h>

#include <esp_log.h>

static const char *TAG = "SetEvtTimeScreen";

SetEvtTimeScreen::SetEvtTimeScreen(SSD1306I2C &display, OutputChannel& ch) :
	Screen(display),
	m_val_orig(ch),
	m_val(ch),
	m_sel_field(0)
{
}

SetEvtTimeScreen::~SetEvtTimeScreen()
{
}

void SetEvtTimeScreen::update()
{
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

		m_display.drawString(0, 0, "Set Time:");
		m_display.drawString(64, 0, m_val.m_name);
		displaySetTime(1, 30, &m_val.m_evt[0], ((m_sel_field <= 3) && m_blank) ? m_sel_field : 0);

		displaySetTime(2, 48, &m_val.m_evt[1], ((m_sel_field > 3) && m_blank) ? (m_sel_field - 3) : 0);

		m_display.display();
	}
	else
	{
		m_update_count--;
	}
}

void SetEvtTimeScreen::receiveEvent(evt_t *evt)
{
	if (evt->id == BTN_SEL_ID)
	{
		if (evt->type == EVT_BTN_PRESS)
		{
			m_sel_field++;
			if (m_sel_field == 7)
				m_sel_field = 1;
		}
		else if (evt->type == EVT_BTN_HOLD)
		{
			m_val_orig = m_val;
			m_val_orig.save();
			m_closed = true;
		}
	}
	else if (evt->id == BTN_BACK_ID && evt->type == EVT_BTN_PRESS)
	{
		m_closed = true;
	}
	else
	{
		updateTime(m_sel_field <= 3 ? &m_val.m_evt[0] : &m_val.m_evt[1], evt, m_sel_field <= 3 ? m_sel_field : m_sel_field - 3);
	}
}

void SetEvtTimeScreen::displaySetTime(uint8_t id, uint8_t height, time_evt_t *tm, uint8_t blank)
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

void SetEvtTimeScreen::updateTime(time_evt_t *tm, evt_t *evt, uint8_t field)
{
	if (tm == NULL || evt == NULL)
		return;
	if (evt->type == EVT_BTN_PRESS || evt->type == EVT_BTN_HOLD_RPT)
	{
		if (evt->id == BTN_UP_ID)
		{
			increment(tm, field);
		}
		else if (evt->id == BTN_DN_ID)
		{
			decrement(tm, field);
		}
		m_update_count = 0;
		update();
	}
}

void SetEvtTimeScreen::increment(time_evt_t *tm, uint8_t field)
{
	if (field == 1)
	{
		tm->hour++;
		if (tm->hour > 23)
			tm->hour = 0;
	}
	else if (field == 2)
	{
		tm->min++;
		if (tm->min > 59)
			tm->min = 0;
	}
	else if (field == 3)
	{
		tm->duration++;
		if (tm->duration > 59)
			tm->duration = 0;
	}
}

void SetEvtTimeScreen::decrement(time_evt_t *tm, uint8_t field)
{
	if (field == 1)
	{
		if (tm->hour == 0)
			tm->hour = 23;
		else
			tm->hour--;
	}
	else if (field == 2)
	{
		if (tm->min == 0)
			tm->min = 59;
		else
			tm->min--;
	}
	else if (field == 3)
	{
		if (tm->duration == 0)
			tm->duration = 59;
		else
			tm->duration--;
	}
}
