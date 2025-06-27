
#include <sstream>
#include <iomanip>

#include <SettingsMenu.h>

#include <SSD1306I2C.h>
#include <icons.h>
#include <iocfg.h>

#include <esp_log.h>

static const char *TAG = "SettingsMenu";

#define MENU_ITEMS {                                                                \
	{.scr_type = SCREEN_TIME, .icon = clock_icon16x16, .name = "CH1 Time"},         \
	{.scr_type = SCREEN_TIME, .icon = clock_icon16x16, .name = "CH2 Time"},         \
	{.scr_type = SCREEN_MOIST, .icon = moisture_icon16x16, .name = "Moisture Thr"}, \
	{.scr_type = SCREEN_WIFI, .icon = wifi1_icon16x16, .name = "WIFI"},             \
	{.scr_type = SCREEN_INFO, .icon = tool_icon16x16, .name = "Info"},              \
}

SettingsMenu::SettingsMenu(SSD1306I2C &display) :
	Screen(display),
	m_menu_items(MENU_ITEMS),
	m_sel_item(0)
{
}

SettingsMenu::~SettingsMenu()
{
}

void SettingsMenu::update()
{
	if (m_refresh)
	{
		m_display.clear();
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
		m_refresh = false;
	}
}

void SettingsMenu::receiveEvent(evt_t *evt)
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
		m_refresh = true;
	}
	else if ((evt->id == BTN_DN_ID) && (evt->type == EVT_BTN_PRESS))
	{
		if (m_sel_item > 0)
			m_sel_item--;
		else
			m_sel_item = (m_menu_items.size() - 1);
		m_refresh = true;
	}
}
