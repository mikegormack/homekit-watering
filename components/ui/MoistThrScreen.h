#pragma once

#include <Screen.h>
#include <OutputChannel.h>

class MoistThrScreen : public Screen
{
public:
	MoistThrScreen(SSD1306I2C& display, uint32_t timeout_tick, uint8_t& val);
	~MoistThrScreen();

	void update() override;
	void receiveEvent(evt_t *evt) override;

private:
	bool m_blank;
	uint8_t& m_val_orig;
	uint8_t m_val;
};
