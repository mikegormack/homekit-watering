#include <Screen.h>

#include <esp_log.h>

static const char *TAG = "Screen";

Screen::Screen(SSD1306I2C &display, uint32_t timeout_tick) : m_display(display),
															 m_update_count(0),
															 m_refresh(true),
															 m_closed(false),
															 m_timeout(timeout_tick),
															 m_timeout_cur(timeout_tick)
{
	m_display.clear();
}

Screen::~Screen()
{
}

void Screen::update()
{
	if (m_timeout_cur > 0)
	{
		m_timeout_cur--;
		if (m_timeout_cur == 0)
		{
			// ESP_LOGI(TAG, "Timeout");
			m_closed = true;
		}
	}
}

void Screen::refreshTimeout()
{
	m_timeout_cur = m_timeout;
	// ESP_LOGI(TAG, "Refresh timeout %lu", m_timeout);
}

bool Screen::isClosed()
{
	bool is_closed = m_closed;
	m_closed = false;
	return is_closed;
}