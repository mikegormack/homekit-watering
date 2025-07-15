
#include <sstream>
#include <iomanip>

#include <SettingsMenu.h>
#include <SetEvtTimeScreen.h>

#include <SSD1306I2C.h>
#include <icons.h>
#include <iocfg.h>

#include <esp_log.h>

static const char *TAG = "SettingsMenu";

SettingsMenu::SettingsMenu(SSD1306I2C &display, uint32_t timeout_tick, MenuCtx& menu_ctx) :
	Screen(display, timeout_tick),
	m_menu_ctx(menu_ctx),
	m_sel_item(0),
	m_cur_menu(&m_menu_base),
	m_scr(nullptr)
{
	createMenu();
}

SettingsMenu::~SettingsMenu()
{
}

void SettingsMenu::createMenu()
{
	m_menu_wifi.emplace_back(wifi1_icon16x16, "Status", nullptr, nullptr);
	m_menu_wifi.emplace_back(wifi1_icon16x16, "Provision", nullptr, nullptr);

	auto ch = m_menu_ctx.m_out_ch.getChannel(CH_ID_WATER_1);
	if (ch != nullptr)
	{
		m_menu_base.emplace_back(clock_icon16x16, "CH1 Time", nullptr, std::make_unique<SetEvtTimeScreen>(m_display, m_timeout, *ch));
	}
	ch = m_menu_ctx.m_out_ch.getChannel(CH_ID_WATER_2);
	if (ch != nullptr)
	{
		m_menu_base.emplace_back(clock_icon16x16, "CH2 Time", nullptr, std::make_unique<SetEvtTimeScreen>(m_display, m_timeout, *ch));
	}
	m_menu_base.emplace_back(moisture_icon16x16, "Moisture Thr", nullptr, nullptr);
	m_menu_base.emplace_back(clock_icon16x16, "WIFI", &m_menu_wifi, nullptr);
	m_menu_base.emplace_back(clock_icon16x16, "Info", nullptr, nullptr);
}

void SettingsMenu::update()
{
	Screen::update();
	if (m_scr != nullptr)
	{
		m_scr->update();
		if (m_scr->isClosed())
		{
			ESP_LOGI(TAG, "Close menu scr");
			m_scr = nullptr;
			m_refresh = true;
		}
		else
		{
			refreshTimeout();
		}
	}
	else if (m_refresh)
	{
		ESP_LOGI(TAG, "Refresh");
		if (m_cur_menu == nullptr)
		{
			ESP_LOGE(TAG, "Menu null");
			return;
		}
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
	if (m_scr != nullptr)
	{
		m_scr->receiveEvent(evt);
	}
	else if (evt->type == EVT_BTN_PRESS)
	{
		if (evt->id == BTN_SEL_ID)
		{
			if (m_cur_menu->at(m_sel_item).sub_menu != nullptr)
			{
				ESP_LOGI(TAG, "Enter submenu");
				m_menu_stack.push_back(m_cur_menu);
				m_ind_stack.push_back(m_sel_item);
				m_cur_menu = m_cur_menu->at(m_sel_item).sub_menu;
				m_sel_item = 0;
			}
			else
			{
				if (m_cur_menu->at(m_sel_item).screen != nullptr)
				{
					m_scr = m_cur_menu->at(m_sel_item).screen.get();
					m_scr->refreshTimeout();
				}
			}
			refreshTimeout();
			m_refresh = true;
		}
		else if (evt->id == BTN_UP_ID)
		{
			if (m_sel_item < (m_cur_menu->size() - 1))
				m_sel_item++;
			else
				m_sel_item = 0;
			refreshTimeout();
			m_refresh = true;
		}
		else if (evt->id == BTN_DN_ID)
		{
			if (m_sel_item > 0)
				m_sel_item--;
			else
				m_sel_item = (m_cur_menu->size() - 1);
			refreshTimeout();
			m_refresh = true;
		}
		else if (evt->id == BTN_BACK_ID)
		{
			if (m_menu_stack.empty() == false)
			{
				m_cur_menu = m_menu_stack.back();
				m_sel_item = m_ind_stack.back();
				m_menu_stack.pop_back();
				m_ind_stack.pop_back();
				refreshTimeout();
				m_refresh = true;
			}
			else
			{
				m_closed = true;
			}
		}
	}
}
