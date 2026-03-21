#pragma once

#include <memory>

#include <Screen.h>
#include <MenuCtx.h>

class HapQrScreen : public Screen
{
public:
    HapQrScreen(SSD1306I2C& display, uint32_t timeout_tick, MenuCtx& menu_ctx);
    ~HapQrScreen();

    void refreshTimeout() override;
    void update() override;
    void receiveEvent(evt_t* evt) override;

private:
    enum class State { QR, Paired, TimedOut };

    MenuCtx&                   m_menu_ctx;
    std::unique_ptr<uint8_t[]> m_qr_code;
    State                      m_state;

    void draw();
};
