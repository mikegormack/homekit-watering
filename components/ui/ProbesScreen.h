#pragma once

#include <Screen.h>
#include <MenuCtx.h>

// List of RS485 soil probes: address, name, moisture, temperature. UP/DN scroll, SEL/BACK close.
class ProbesScreen : public Screen
{
public:
	ProbesScreen(SSD1306I2C& display, uint32_t timeout_tick, MenuCtx& ctx);
	~ProbesScreen() = default;

	void open() override;
	void update() override;
	void receiveEvent(evt_t* evt) override;

private:
	static constexpr int ROWS = 4;

	MenuCtx& m_ctx;
	int      m_first = 0;

	void draw();
};
