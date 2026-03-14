#pragma once

#include <memory>

#include <Screen.h>
#include <MenuCtx.h>

typedef enum
{
    PROV_STATE_QR,
    PROV_STATE_SUCCESS,
    PROV_STATE_ERROR,
} prov_state_t;

class WifiProvScreen : public Screen
{
public:
    WifiProvScreen(SSD1306I2C& display, MenuCtx& menu_ctx);
    ~WifiProvScreen();

    void update() override;
    void refreshTimeout() override;
    void receiveEvent(evt_t* evt) override;

private:
    MenuCtx&                   m_menu_ctx;
    std::unique_ptr<uint8_t[]> m_qr_code;
    prov_state_t               m_state;
    int                        m_success_count;

    void drawQR();
    void drawSuccess();
    void drawError();
    void cancel();
};
