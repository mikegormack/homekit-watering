#pragma once

#include <Screen.h>
#include <MenuCtx.h>

// Per-zone moisture threshold: scheduled runs are skipped while the zone's probe reads at or above it.
class MoistThrScreen : public Screen
{
public:
	MoistThrScreen(SSD1306I2C& display, uint32_t timeout_tick, MenuCtx& ctx, int ch);
	~MoistThrScreen();

	void open() override;
	void update() override;
	void receiveEvent(evt_t *evt) override;

private:
	MenuCtx& m_menu_ctx;
	int      m_ch;
	bool     m_blank;
	uint8_t  m_val; // working copy of threshold, written back on confirm
};
