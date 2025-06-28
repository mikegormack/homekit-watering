
#include <sstream>
#include <iomanip>

#include <SettingsMenu.h>

#include <SSD1306I2C.h>
#include <icons.h>
#include <iocfg.h>

#include <esp_log.h>

static const char *TAG = "SettingsMenu";

const std::vector<menu_item> SettingsMenu::s_main_menu =
{
	{.icon = clock_icon16x16,       .name = "CH1 Time",     .sub_menu = nullptr },
	{.icon = clock_icon16x16,       .name = "CH2 Time",     .sub_menu = nullptr },
	{.icon = moisture_icon16x16,    .name = "Moisture Thr", .sub_menu = nullptr },
	{.icon = wifi1_icon16x16,       .name = "WIFI",         .sub_menu = &s_wifi_menu },
	{.icon = tool_icon16x16,        .name = "Info",         .sub_menu = nullptr }
};

const std::vector<menu_item> SettingsMenu::s_wifi_menu =
{
	{.icon = wifi1_icon16x16,       .name = "Status",       .sub_menu = nullptr },
	{.icon = tool_icon16x16,        .name = "Provision",    .sub_menu = nullptr }
};

/*#define MENU_ITEMS {                                                                \
	{.scr_type = SCREEN_TIME, .icon = clock_icon16x16, .name = "CH1 Time"},         \
	{.scr_type = SCREEN_TIME, .icon = clock_icon16x16, .name = "CH2 Time"},         \
	{.scr_type = SCREEN_MOIST, .icon = moisture_icon16x16, .name = "Moisture Thr"}, \
	{.scr_type = SCREEN_WIFI, .icon = wifi1_icon16x16, .name = "WIFI"},             \
	{.scr_type = SCREEN_INFO, .icon = tool_icon16x16, .name = "Info"},              \
}*/

SettingsMenu::SettingsMenu(SSD1306I2C &display) : Screen(display),
	m_sel_item(0),
	m_cur_menu(&s_main_menu)
{
}

SettingsMenu::~SettingsMenu()
{
}

void SettingsMenu::update()
{
	if (m_refresh)
	{
		ESP_LOGI(TAG, "Refresh");
		if (m_cur_menu == nullptr)
		{
			ESP_LOGE(TAG, "Menu null");
			return;
		}
		ESP_LOGI(TAG, "Menu sz=%d", m_cur_menu->size());
		m_display.clear();
		m_display.setFont(ArialMT_Plain_16);

		uint8_t prev_item = 0;
		if (m_sel_item > 0)
		{
			prev_item = m_sel_item - 1;
		}
		else
		{
			prev_item = m_cur_menu->size() - 1;
		}
		uint8_t next_item = m_sel_item + 1;
		if (next_item > (m_cur_menu->size() - 1))
			next_item = 0;

		m_display.drawBitmap(2, 0, m_cur_menu->at(prev_item).icon, 16, 16);
		m_display.drawString(22, 0, m_cur_menu->at(prev_item).name);

		m_display.drawRect(0, 20, 128, 24);
		m_display.drawBitmap(2, 24, m_cur_menu->at(m_sel_item).icon, 16, 16);
		m_display.drawString(22, 24, m_cur_menu->at(m_sel_item).name);

		m_display.drawBitmap(2, 48, m_cur_menu->at(next_item).icon, 16, 16);
		m_display.drawString(22, 48, m_cur_menu->at(next_item).name);
		m_display.display();
		m_refresh = false;
	}
}

void SettingsMenu::receiveEvent(evt_t *evt)
{
	// if menu screen displayed pass on event to it.
	if (evt->type == EVT_BTN_PRESS)
	{
		if (evt->id == BTN_SEL_ID)
		{
			if (m_cur_menu->at(m_sel_item).sub_menu != nullptr)
			{
				ESP_LOGI(TAG, "Enter submenu");
				m_menu_stack.push_back(m_cur_menu);
				m_cur_menu = m_cur_menu->at(m_sel_item).sub_menu;
				m_sel_item = 0;
			}
			m_refresh = true;
		}
		else if (evt->id == BTN_UP_ID)
		{
			if (m_sel_item < (m_cur_menu->size() - 1))
				m_sel_item++;
			else
				m_sel_item = 0;
			m_refresh = true;
		}
		else if (evt->id == BTN_DN_ID)
		{
			if (m_sel_item > 0)
				m_sel_item--;
			else
				m_sel_item = (m_cur_menu->size() - 1);
			m_refresh = true;
		}
		else if (evt->id == BTN_BACK_ID)
		{
			if (m_menu_stack.empty() == false)
			{
				m_cur_menu = m_menu_stack.back();
				m_menu_stack.pop_back();
			}
		}
	}
}
