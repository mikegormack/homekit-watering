#pragma once

#include <Screen.h>
#include <OutputChannel.h>

class SchedEnableScreen : public Screen
{
public:
    SchedEnableScreen(SSD1306I2C &display, uint32_t timeout_tick, OutputChannel &ch);
    ~SchedEnableScreen();

    void open() override;
    void update() override;
    void receiveEvent(evt_t *evt) override;

private:
    OutputChannel &m_ch;
    bool           m_enabled; // working copy
};
