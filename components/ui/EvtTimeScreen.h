#pragma once

#include <Screen.h>
#include <OutputChannel.h>

class EvtTimeScreen : public Screen
{
public:
	EvtTimeScreen(SSD1306I2C &display, uint32_t timeout_tick, OutputChannel &ch);
	~EvtTimeScreen();

	void update() override;
	void open() override;
	void receiveEvent(evt_t *evt) override;

private:
	bool m_blank = false;
	OutputChannel& m_val_orig;
	OutputChannel  m_val;  // working copy; committed to m_val_orig on save, discarded on cancel
	uint8_t m_sel_field;
	void displaySetTime(uint8_t id, uint8_t height, const time_evt_t *tm, uint8_t blank);
	bool updateTime(time_evt_t *tm, evt_t *evt, uint8_t field);
	void increment(time_evt_t *tm, uint8_t field);
	void decrement(time_evt_t *tm, uint8_t field);
};
