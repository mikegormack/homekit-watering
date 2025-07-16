#pragma once

#include <Screen.h>
#include <OutputChannel.h>

class EvtTimeScreen : public Screen
{
public:
	EvtTimeScreen(SSD1306I2C &display, uint32_t timeout_tick, OutputChannel &ch);
	~EvtTimeScreen();

	void update() override;
	void receiveEvent(evt_t *evt) override;

private:
	bool m_blank;
	OutputChannel& m_val_orig;
	OutputChannel& m_val;
	uint8_t m_sel_field;
	void displaySetTime(uint8_t id, uint8_t height, time_evt_t *tm, uint8_t blank);
	void updateTime(time_evt_t *tm, evt_t *evt, uint8_t field);
	void increment(time_evt_t *tm, uint8_t field);
	void decrement(time_evt_t *tm, uint8_t field);
};
