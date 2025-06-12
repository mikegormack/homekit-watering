
#include <ctime>
#include <sstream>
#include <iomanip>

#include <setOnTimeScreen.h>

#include <SSD1306I2C.h>
#include <icons.h>

setOnTimeScreen::setOnTimeScreen(SSD1306I2C& display) :
	screen(display)
{
	m_display.clear();
}

setOnTimeScreen::~setOnTimeScreen()
{
}

void setOnTimeScreen::update()
{
	if (m_update_count == 0)
	{
		m_update_count = 50;


		m_display.setFont(ArialMT_Plain_16);
		m_display.drawString(50, 20, "00:00");

		/*m_display.setColor(WHITE);
		m_display.clear();

		m_display.drawBitmap(112, 0, tool_icon16x16, 16, 16);

		m_display.drawBitmap(42, 22, water_tap_icon16x16, 16, 16);
		m_display.setFont(ArialMT_Plain_16);
		m_display.drawString(60, 20, "Off");

		m_display.drawBitmap(13, 44, moisture_icon16x16, 16, 16);
		m_display.drawProgressBar(30,48,30,6,50);

		m_display.drawBitmap(63, 44, clock_icon16x16, 16, 16);

		m_display.setFont(ArialMT_Plain_10);
		m_display.drawString(81, 46, "00");
		m_display.drawString(94, 46, ":");
		m_display.drawString(99, 46, "00");*/



		m_display.display();
	}
	else
	{
		m_update_count--;
	}
}

void setOnTimeScreen::sendBtnEvent(uint8_t btn)
{

}
/*
void homeScreen::update_clock()
{
	std::time_t now;
    std::time(&now);
    std::tm* local = std::localtime(&now);

    m_display.setFont(ArialMT_Plain_10);

	std::stringstream ss;
	ss << std::setw(2) << std::setfill('0') << +local->tm_hour;
	m_display.drawString(40, 0, ss.str());
	m_display.drawString(53, 0, ":");

	ss.clear();
	ss.str("");

	ss << std::setw(2) << std::setfill('0') << +local->tm_min;
	m_display.drawString(58, 0, ss.str());
	m_display.drawString(71, 0, ":");

	ss.clear();
	ss.str("");

	ss << std::setw(2) << std::setfill('0') << +local->tm_sec;
	m_display.drawString(76, 0, ss.str());
}*/

