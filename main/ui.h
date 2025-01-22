#pragma once
#include <SSD1306I2C.h>
#include <homeScreen.h>

class ui
{
private:
	SSD1306I2C& m_display;


public:
	ui(SSD1306I2C& display);
	~ui();

	static void ui_thread_entry(void *p);

	homeScreen m_home_screen;
};
