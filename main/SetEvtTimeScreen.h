#pragma once

#include <MenuScreen.h>

#include <TimeTypes.h>

class SetEvtTimeScreen : public MenuScreen
{
public:
	SetEvtTimeScreen(SSD1306I2C &display, ch_time_t &val);
	~SetEvtTimeScreen();

	void update() override;
	void receiveEvent(evt_t *evt) override;

private:
	bool m_blank;
	ch_time_t &m_val;
	uint8_t m_sel_field;
	void displaySetTime(uint8_t id, uint8_t height, time_evt_t *tm, uint8_t blank);
	void updateTime(time_evt_t *tm, evt_t *evt, uint8_t field);
	void incremnent(time_evt_t *tm, uint8_t field);
	void decrement(time_evt_t *tm, uint8_t field);
};
