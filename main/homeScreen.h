#pragma once

#include <screen.h>

class homeScreen : public screen
{
public:
	homeScreen(SSD1306I2C &display);
	~homeScreen();

	void update() override;
	void receiveEvent(evt_t *evt) override;

private:
	void update_clock();
	void show_wifi();
};
