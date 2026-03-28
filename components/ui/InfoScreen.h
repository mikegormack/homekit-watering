#pragma once

#include <Screen.h>

class InfoScreen : public Screen
{
public:
    InfoScreen(SSD1306I2C& display, uint32_t timeout_tick);
    ~InfoScreen() = default;

    void update() override;
    void receiveEvent(evt_t* evt) override;
};
