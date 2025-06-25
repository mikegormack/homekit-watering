
#include <sstream>
#include <iomanip>

#include <menuScreen.h>

#include <SSD1306I2C.h>
#include <icons.h>
#include <iocfg.h>

#include <esp_log.h>

static const char *TAG = "menuScreen";

#define MENU_ITEMS {                                            \
	{ .icon = clock_icon16x16,      .name = "CH1 Time"      },  \
	{ .icon = clock_icon16x16,      .name = "CH2 Time"      },  \
	{ .icon = moisture_icon16x16,  .name = "Moisture Thr"   },  \
	{ .icon = wifi1_icon16x16,      .name = "WIFI"          },  \
	{ .icon = tool_icon16x16,       .name = "Info"          },  \
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

		uint8_t prev_item = 0;
		if (m_sel_item > 0)
		{
			prev_item = m_sel_item - 1;
		}
		else
		{
			prev_item = m_menu_items.size() - 1;
		}
		uint8_t next_item = m_sel_item + 1;
		if (next_item > (m_menu_items.size() - 1))
			next_item = 0;

		m_display.drawBitmap(1, 0, m_menu_items[prev_item].icon, 16, 16);
		m_display.drawString(21, 0, m_menu_items[prev_item].name);

		m_display.drawRect(0, 20, 128, 24);
		m_display.drawBitmap(1, 24, m_menu_items[m_sel_item].icon, 16, 16);
		m_display.drawString(21, 24, m_menu_items[m_sel_item].name);

		m_display.drawBitmap(1, 48, m_menu_items[next_item].icon, 16, 16);
		m_display.drawString(21, 48, m_menu_items[next_item].name);
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
		else
			m_sel_item = 0;
	}
	else if ((evt->id == BTN_DN_ID) && (evt->type == EVT_BTN_PRESS))
	{
		if (m_sel_item > 0)
			m_sel_item--;
		else
			m_sel_item = (m_menu_items.size() - 1);
	}
}
