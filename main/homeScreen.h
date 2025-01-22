#pragma once

#include <screen.h>

class homeScreen : public screen
{
public:
	homeScreen(SSD1306I2C& display);
	~homeScreen();

	void update() override;
private:
	void update_clock();
	void show_wifi();
};
