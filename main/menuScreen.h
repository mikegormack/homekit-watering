#pragma once

#include <screen.h>

#include <vector>

typedef struct
{
	const char* name;
} menu_item_t;

class menuScreen : public screen
{
public:
	menuScreen(SSD1306I2C &display);
	~menuScreen();

	void update() override;
	void receiveEvent(evt_t *evt) override;

private:
	std::vector<menu_item_t> m_menu_items;
	uint8_t m_sel_item;
};
