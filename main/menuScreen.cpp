
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
	{ .name = "CH2 Time"}           \
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
			m_display.drawString(16, 8, m_menu_items[m_sel_item - 1].name);
		if (m_sel_item < m_menu_items.size())
			m_display.drawString(16, 24, m_menu_items[m_sel_item].name);
		if ((m_sel_item + 1) < m_menu_items.size())
			m_display.drawString(16, 40, m_menu_items[m_sel_item + 1].name);

		// line 2

		// line 3

		m_display.display();
	}
	else
	{
		m_update_count--;
	}
}

void menuScreen::receiveEvent(evt_t *evt)
{
	/*if (evt->id == BTN_SEL_ID)
	{
		if (evt->type == EVT_BTN_PRESS)
		{
			m_sel_field++;
			if (m_sel_field == 7)
				m_sel_field = 1;
			ESP_LOGI(TAG, "Sel field %d", m_sel_field);
		}
	}
	else
	{
		updateTime(m_sel_field <= 3 ? &m_ch1 : &m_ch2, evt, m_sel_field <= 3 ? m_sel_field : m_sel_field - 3);
	}*/
}
