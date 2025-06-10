#pragma once

#include <SSD1306I2C.h>
#include <icons.h>

class screen
{
public:
	screen(SSD1306I2C& display) :
		m_display(display),
		m_update_count(0)
	{
	}

	~screen()
	{

	}

protected:
	SSD1306I2C& m_display;

	const uint8_t wifi1_icon16x16[32] = WIFI1_ICON16X16;

	const uint8_t signal1_icon16x16[32] = SIGNAL1_ICON16X16;
	const uint8_t signal2_icon16x16[32] = SIGNAL2_ICON16X16;
	const uint8_t signal3_icon16x16[32] = SIGNAL3_ICON16X16;
	const uint8_t signal4_icon16x16[32] = SIGNAL4_ICON16X16;
	const uint8_t cancel_icon16x16[32] = CANCEL_ICON16X16;

	const uint8_t moisture_icon16x16[32] = HUMIDITY2_ICON16X16;
	const uint8_t clock_icon16x16[32] = CLOCK_ICON16X16;
	const uint8_t tool_icon16x16[32] = TOOL_ICON16X16;
	const uint8_t water_tap_icon16x16[32] = WATER_TAP_ICON16X16;

	int m_update_count;

	virtual void update() = 0;

	virtual void sendBtnEvent(uint8_t btn) = 0;
};
