#pragma once

#include <Screen.h>

class HomeScreen : public Screen
{
public:
	HomeScreen(SSD1306I2C &display);
	~HomeScreen();

	void update() override;
	void receiveEvent(evt_t *evt) override;

private:
	void update_clock();
	void show_wifi();
};
