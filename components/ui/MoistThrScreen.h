#pragma once

#include <Screen.h>
#include <MenuCtx.h>

class MoistThrScreen : public Screen
{
public:
	MoistThrScreen(SSD1306I2C& display, uint32_t timeout_tick, MenuCtx& ctx);
	~MoistThrScreen();

	void open() override;
	void update() override;
	void receiveEvent(evt_t *evt) override;

private:
	MenuCtx& m_menu_ctx;
	bool     m_blank;
	uint8_t  m_val; // working copy of threshold, written back on confirm
};
