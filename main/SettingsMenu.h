#pragma once

#include <Screen.h>
#include <MenuScreen.h>
#include <TimeTypes.h>

#include <vector>
#include <memory>

typedef enum
{
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
};

class SettingsMenu : public Screen
{
public:
	SettingsMenu(SSD1306I2C &display);
	~SettingsMenu();

	void update() override;
	void receiveEvent(evt_t *evt) override;

private:
	static const std::vector<menu_item> s_main_menu;
	static const std::vector<menu_item> s_wifi_menu;

	uint8_t m_sel_item;
	const std::vector<menu_item>* m_cur_menu;
};
