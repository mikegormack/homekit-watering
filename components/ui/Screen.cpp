#include <Screen.h>
#include <icons.h>

#include <esp_log.h>

const uint8_t wifi1_icon16x16[32]     = WIFI1_ICON16X16;
const uint8_t bullet_icon16x16[32]    = BULLET_ICON16X16;
const uint8_t up_arrow_icon16x16[32]  = ARROW_UP_ICON16X16;
const uint8_t dn_arrow_icon16x16[32]  = ARROW_DOWN_ICON16X16;
const uint8_t signal1_icon16x16[32]   = SIGNAL1_ICON16X16;
const uint8_t signal2_icon16x16[32]   = SIGNAL2_ICON16X16;
const uint8_t signal3_icon16x16[32]   = SIGNAL3_ICON16X16;
const uint8_t signal4_icon16x16[32]   = SIGNAL4_ICON16X16;
const uint8_t cancel_icon16x16[32]    = CANCEL_ICON16X16;
const uint8_t moisture_icon16x16[32]  = HUMIDITY2_ICON16X16;
const uint8_t clock_icon16x16[32]     = CLOCK_ICON16X16;
const uint8_t timer_icon16x16[32]     = TIMER_ICON16X16;
const uint8_t tool_icon16x16[32]      = TOOL_ICON16X16;
const uint8_t water_tap_icon16x16[32] = WATER_TAP_ICON16X16;
const uint8_t apple_icon16x16[32]     = APPLE_ICON16X16;

//static const char *TAG = "Screen";

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
			m_closed = true;
		}
	}
}

void Screen::refreshTimeout()
{
	m_timeout_cur = m_timeout;
}

bool Screen::isClosed()
{
	bool is_closed = m_closed;
	m_closed = false;
	return is_closed;
}

void Screen::open()
{
	m_closed       = false;
	m_refresh      = true;
	m_update_count = 0;
	refreshTimeout();
}