#include <ui.h>
#include <SSD1306I2C.h>
#include <icons_macros.h>

ui::ui(SSD1306I2C& display) :
	m_display(display)
{
}

ui::~ui()
{
}

void ui::homescreen()
{
	m_display.setColor(WHITE);
	m_display.clear();
	m_display.drawBitmap(0, -1, wifi1_icon16x16, 16, 16);
	m_display.drawBitmap(9, 0, signal4_icon16x16, 16, 16);
	m_display.drawBitmap(112, 0, tool_icon16x16, 16, 16);


	m_display.setFont(ArialMT_Plain_10);
	m_display.drawString(40, 0, "00");
	m_display.drawString(53, 0, ":");
	m_display.drawString(58, 0, "00");
	m_display.drawString(71, 0, ":");
	m_display.drawString(76, 0, "00");

	m_display.drawBitmap(42, 22, water_tap_icon16x16, 16, 16);
	m_display.setFont(ArialMT_Plain_16);
	m_display.drawString(60, 20, "Off");

	m_display.drawBitmap(13, 44, moisture_icon16x16, 16, 16);
	m_display.drawProgressBar(30,48,30,6,50);

	m_display.drawBitmap(63, 44, clock_icon16x16, 16, 16);

	m_display.setFont(ArialMT_Plain_10);
	m_display.drawString(81, 46, "00");
	m_display.drawString(94, 46, ":");
	m_display.drawString(99, 46, "00");

	m_display.display();
}