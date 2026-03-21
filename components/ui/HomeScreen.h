#pragma once

#include <Screen.h>
#include <MenuCtx.h>

class HomeScreen : public Screen
{
public:
	HomeScreen(SSD1306I2C &display, uint32_t timeout_tick, MenuCtx &menu_ctx);
	~HomeScreen();

	void update() override;
	void receiveEvent(evt_t *evt) override;

private:
	MenuCtx &m_menu_ctx;
	int      m_channel;        // 0 or 1
	int      m_channel_ticks;  // counts down to channel switch

	void update_clock();
	void show_wifi();
	void show_channel();
};
