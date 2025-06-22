
#include <sstream>
#include <iomanip>

#include <menuScreen.h>

#include <SSD1306I2C.h>
#include <icons.h>
#include <iocfg.h>

#include <esp_log.h>

static const char *TAG = "menuScreen";

#define MENU_ITEMS {                \
	{ .name = "CH1 Time"},          \
	{ .name = "CH2 Time"},           \
	{ .name = "Moisture Thr"}       \
}

menuScreen::menuScreen(SSD1306I2C &display) : screen(display),
											  m_menu_items(MENU_ITEMS),
											  m_sel_item(0)
{

}

menuScreen::~menuScreen()
{
}

void menuScreen::update()
{
	if (m_update_count == 0)
	{
		m_display.clear();
		m_update_count = 100;
		m_display.setFont(ArialMT_Plain_16);

		if (m_sel_item > 0)
		{
			m_display.drawBitmap(0, 8, up_arrow_icon16x16, 16, 16);
			m_display.drawString(16, 8, m_menu_items[m_sel_item - 1].name);
		}
		if (m_sel_item < m_menu_items.size())
		{
			m_display.drawBitmap(0, 24, bullet_icon16x16, 16, 16);
			m_display.drawString(16, 24, m_menu_items[m_sel_item].name);
		}
		if ((m_sel_item + 1) < m_menu_items.size())
		{
			m_display.drawBitmap(0, 40, dn_arrow_icon16x16, 16, 16);
			m_display.drawString(16, 40, m_menu_items[m_sel_item + 1].name);
		}
		m_display.display();
	}
	else
	{
		m_update_count--;
	}
}

void menuScreen::receiveEvent(evt_t *evt)
{
	if ((evt->id == BTN_SEL_ID) && (evt->type == EVT_BTN_PRESS))
	{

	}
	else if ((evt->id == BTN_UP_ID) && (evt->type == EVT_BTN_PRESS))
	{
		if (m_sel_item < (m_menu_items.size() - 1))
			m_sel_item++;
	}
	else if ((evt->id == BTN_DN_ID) && (evt->type == EVT_BTN_PRESS))
	{
		if (m_sel_item > 0)
			m_sel_item--;
	}
}
