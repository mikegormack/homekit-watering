#pragma once

#include <Screen.h>
#include <MenuCtx.h>

#include <vector>
#include <memory>

typedef enum
{
	SCREEN_NONE,
	SCREEN_TIME,
	SCREEN_MOIST,
	SCREEN_WIFI,
	SCREEN_INFO,
	// Add others
} screen_type_t;

struct menu_item
{
	const uint8_t *icon;
	const char *name;
	const std::vector<menu_item>* sub_menu;
	std::unique_ptr<Screen> screen;
};

class SettingsMenu : public Screen
{
public:
	SettingsMenu(SSD1306I2C &display, uint32_t timeout_tick, MenuCtx& menu_ctx);
	~SettingsMenu();

	void update() override;
	void receiveEvent(evt_t *evt) override;

private:
	MenuCtx& m_menu_ctx;
	uint8_t m_sel_item;

	std::vector<menu_item> m_menu_base;
	std::vector<menu_item> m_menu_wifi;

	const std::vector<menu_item>* m_cur_menu;
	std::vector<const std::vector<menu_item>*> m_menu_stack;
	std::vector<uint8_t> m_ind_stack;
	Screen* m_scr;

	uint8_t m_moist_val = 50;

	void createMenu(void);
};
