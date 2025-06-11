#pragma once

#include <screen.h>

class setOnTimeScreen : public screen
{
public:
	setOnTimeScreen(SSD1306I2C& display);
	~setOnTimeScreen();

	void update() override;
	void sendBtnEvent(uint8_t btn) override;
private:
	/*void update_clock();
	void show_wifi();*/
};
