
#include <stdio.h>

#include <EvtTimeScreen.h>

#include <SSD1306I2C.h>
#include <icons.h>
#include <iocfg.h>

#include <esp_log.h>

//static const char *TAG = "EvtTimeScreen";

EvtTimeScreen::EvtTimeScreen(SSD1306I2C &display, uint32_t timeout_tick, OutputChannel &ch) : Screen(display, timeout_tick),
																							  m_val_orig(ch),
																							  m_val(ch),
																							  m_sel_field(0)
{
}

EvtTimeScreen::~EvtTimeScreen()
{
}

void EvtTimeScreen::open()
{
	m_val = m_val_orig;  // reset working copy from original
	m_sel_field = 0;
	Screen::open();
}

void EvtTimeScreen::update()
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

		m_display.drawString(0, 0, "Set Time:");
		m_display.drawString(70, 0, m_val.getName());
		displaySetTime(1, 30, &m_val.getEvents()[0], ((m_sel_field <= 3) && m_blank) ? m_sel_field : 0);

		displaySetTime(2, 48, &m_val.getEvents()[1], ((m_sel_field > 3) && m_blank) ? (m_sel_field - 3) : 0);

		m_display.display();
	}
	else
	{
		m_update_count--;
	}
}

void EvtTimeScreen::receiveEvent(evt_t *evt)
{
	if (evt->id == BTN_SEL_ID)
	{
		if (evt->type == EVT_BTN_PRESS)
		{
			m_sel_field++;
			if (m_sel_field == 7)
				m_sel_field = 1;
			refreshTimeout();
		}
		else if (evt->type == EVT_BTN_HOLD)
		{
			m_val_orig = m_val;
			m_val_orig.save();
			m_sel_field = 0;
			m_closed = true;
		}
	}
	else if (evt->id == BTN_BACK_ID && evt->type == EVT_BTN_PRESS)
	{
		m_sel_field = 0;
		m_closed = true;
	}
	else
	{
		int        idx   = (m_sel_field <= 3) ? 0 : 1;
		int        field = (m_sel_field <= 3) ? m_sel_field : m_sel_field - 3;
		time_evt_t copy  = m_val.getEvents()[idx];
		if (updateTime(&copy, evt, field))
		{
			m_val.setEvent(idx, copy);
			m_update_count = 0;
			update();
		}
		refreshTimeout();
	}
}

void EvtTimeScreen::displaySetTime(uint8_t id, uint8_t height, const time_evt_t *tm, uint8_t blank)
{
	char buf[8];
	snprintf(buf, sizeof(buf), "%d.", id);
	m_display.drawString(0, height, buf);
	m_display.drawBitmap(16, height, clock_icon16x16, 16, 16);
	if (blank != 1)
	{
		snprintf(buf, sizeof(buf), "%02d", tm->hour);
		m_display.drawString(36, height, buf);
	}
	m_display.drawString(57, height, ":");
	if (blank != 2)
	{
		snprintf(buf, sizeof(buf), "%02d", tm->min);
		m_display.drawString(61, height, buf);
	}
	m_display.drawBitmap(84, height, timer_icon16x16, 16, 16);
	if (blank != 3)
	{
		snprintf(buf, sizeof(buf), "%02d", tm->duration);
		m_display.drawString(104, height, buf);
	}
}

bool EvtTimeScreen::updateTime(time_evt_t *tm, evt_t *evt, uint8_t field)
{
	if (tm == NULL || evt == NULL)
		return false;
	if (evt->type == EVT_BTN_PRESS || evt->type == EVT_BTN_HOLD_RPT)
	{
		if (evt->id == BTN_UP_ID)
			increment(tm, field);
		else if (evt->id == BTN_DN_ID)
			decrement(tm, field);
		return true;
	}
	return false;
}

void EvtTimeScreen::increment(time_evt_t *tm, uint8_t field)
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

void EvtTimeScreen::decrement(time_evt_t *tm, uint8_t field)
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
