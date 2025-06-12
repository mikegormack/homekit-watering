#pragma once

#include <screen.h>

typedef struct
{
	uint8_t hour;
	uint8_t min;
	uint8_t duration;
} time_val_t;

class setOnTimeScreen : public screen
{
public:
	setOnTimeScreen(SSD1306I2C &display);
	~setOnTimeScreen();

	void update() override;
	void receiveEvent(evt_t *evt) override;

private:
	uint8_t m_sel_field;
	bool m_blank;
	void displaySetTime(uint8_t id, uint8_t height, time_val_t *tm, uint8_t blank);
};
