#pragma once

#include <Screen.h>
#include <MenuCtx.h>

class WifiStatusScreen : public Screen
{
public:
    WifiStatusScreen(SSD1306I2C& display, uint32_t timeout_tick, MenuCtx& menu_ctx);
    ~WifiStatusScreen();

    void update() override;
    void receiveEvent(evt_t* evt) override;

private:
    MenuCtx& m_menu_ctx;
};
