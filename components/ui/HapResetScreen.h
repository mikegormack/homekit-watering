#pragma once

#include <Screen.h>
#include <MenuCtx.h>

class HapResetScreen : public Screen
{
public:
    HapResetScreen(SSD1306I2C& display, uint32_t timeout_tick, MenuCtx& menu_ctx);
    ~HapResetScreen();

    void refreshTimeout() override;
    void update() override;
    void receiveEvent(evt_t* evt) override;

private:
    MenuCtx& m_menu_ctx;

    void draw();
};
