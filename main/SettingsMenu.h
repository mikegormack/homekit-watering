#pragma once

#include <screen.h>
#include <TimeTypes.h>

#include <vector>

typedef enum
{
	SCREEN_TIME,
	SCREEN_MOIST,
	SCREEN_WIFI,
	SCREEN_INFO,
	// Add others
} screen_type_t;

typedef struct
{
	screen_type_t scr_type;
	const uint8_t *icon;
	const char *name;

} menu_item_t;

extern const menu_item_t menu_items_array[];
extern const size_t menu_items_count;

class SettingsMenu : public Screen
{
public:
	SettingsMenu(SSD1306I2C &display);
	~SettingsMenu();

	void update() override;
	void receiveEvent(evt_t *evt) override;

private:
	std::vector<menu_item_t> m_menu_items;
	uint8_t m_sel_item;
};
