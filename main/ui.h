#pragma once
#include <SSD1306I2C.h>
#include <icons_macros.h>

class ui
{
private:
	SSD1306I2C& m_display;

	const uint8_t wifi1_icon16x16[32] = WIFI1_ICON16X16;
	const uint8_t signal4_icon16x16[32] = SIGNAL4_ICON16X16;
	const uint8_t moisture_icon16x16[32] = HUMIDITY2_ICON16X16;
	const uint8_t clock_icon16x16[32] = CLOCK_ICON16X16;
	const uint8_t tool_icon16x16[32] = TOOL_ICON16X16;
	const uint8_t water_tap_icon16x16[32] = WATER_TAP_ICON16X16;
public:
	ui(SSD1306I2C& display);
	~ui();

	void homescreen();
};
