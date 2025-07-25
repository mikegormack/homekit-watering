#pragma once

#include <SSD1306I2C.h>
#include <icons.h>

typedef enum
{
	EVT_BTN_PRESS,
	EVT_BTN_HOLD,
	EVT_BTN_HOLD_RPT,
	EVT_BTN_LONG_HOLD,
} evt_type_t;

typedef struct
{
	uint8_t id;
	evt_type_t type;
} evt_t;

const uint8_t wifi1_icon16x16[32] = WIFI1_ICON16X16;

const uint8_t bullet_icon16x16[32] = BULLET_ICON16X16;
const uint8_t up_arrow_icon16x16[32] = ARROW_UP_ICON16X16;
const uint8_t dn_arrow_icon16x16[32] = ARROW_DOWN_ICON16X16;

const uint8_t signal1_icon16x16[32] = SIGNAL1_ICON16X16;
const uint8_t signal2_icon16x16[32] = SIGNAL2_ICON16X16;
const uint8_t signal3_icon16x16[32] = SIGNAL3_ICON16X16;
const uint8_t signal4_icon16x16[32] = SIGNAL4_ICON16X16;
const uint8_t cancel_icon16x16[32] = CANCEL_ICON16X16;

const uint8_t moisture_icon16x16[32] = HUMIDITY2_ICON16X16;
const uint8_t clock_icon16x16[32] = CLOCK_ICON16X16;
const uint8_t timer_icon16x16[32] = TIMER_ICON16X16;
const uint8_t tool_icon16x16[32] = TOOL_ICON16X16;
const uint8_t water_tap_icon16x16[32] = WATER_TAP_ICON16X16;

class Screen
{
public:
	Screen(SSD1306I2C &display, uint32_t timeout_tick);

	~Screen();

	virtual void update();

	virtual void refreshTimeout();

	virtual void receiveEvent(evt_t *evt) = 0;

	virtual bool isClosed();

protected:
	SSD1306I2C &m_display;

	int m_update_count;
	bool m_refresh;
	bool m_closed;
	uint32_t m_timeout;
	uint32_t m_timeout_cur;
};
