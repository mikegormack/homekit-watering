#pragma once

#include <Screen.h>
#include <MenuCtx.h>

class TimezoneScreen : public Screen
{
public:
	TimezoneScreen(SSD1306I2C &display, uint32_t timeout_tick, MenuCtx &menu_ctx);
	~TimezoneScreen();

	void update() override;
	void receiveEvent(evt_t *evt) override;

private:
	MenuCtx &m_menu_ctx;
	int      m_sel;
};
