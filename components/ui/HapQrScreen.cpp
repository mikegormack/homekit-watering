
#include <HapQrScreen.h>
#include <SSD1306I2C.h>
#include <iocfg.h>

#include <qrcodegen.h>
#include <esp_log.h>

static const char* TAG = "HapQrScreen";

HapQrScreen::HapQrScreen(SSD1306I2C& display, uint32_t timeout_tick, MenuCtx& menu_ctx)
    : Screen(display, timeout_tick), m_menu_ctx(menu_ctx), m_qr_code(nullptr), m_state(State::QR)
{
}

HapQrScreen::~HapQrScreen()
{
}

void HapQrScreen::refreshTimeout()
{
    Screen::refreshTimeout();

    m_qr_code = nullptr;
    m_refresh  = true;

    // Check if already paired
    if (m_menu_ctx.hap_is_paired && m_menu_ctx.hap_is_paired())
    {
        m_state = State::Paired;
        return;
    }

    // If pairing window timed out, reopen it
    if (m_menu_ctx.hap_pairing_timed_out)
    {
        m_state = State::TimedOut;
        if (m_menu_ctx.hap_reopen_pairing)
            m_menu_ctx.hap_reopen_pairing();
        m_state = State::QR;
    }
    else
    {
        m_state = State::QR;
    }

    if (m_menu_ctx.hap_setup_uri.empty())
    {
        ESP_LOGW(TAG, "No HomeKit setup URI available");
        return;
    }

    const char* uri = m_menu_ctx.hap_setup_uri.c_str();

    static const int buf_len = qrcodegen_BUFFER_LEN_MAX;
    auto tmp = std::make_unique<uint8_t[]>(buf_len);
    auto out = std::make_unique<uint8_t[]>(buf_len);

    if (qrcodegen_encodeText(uri, tmp.get(), out.get(), qrcodegen_Ecc_MEDIUM,
                             qrcodegen_VERSION_MIN, qrcodegen_VERSION_MAX,
                             qrcodegen_Mask_AUTO, true))
    {
        m_qr_code = std::move(out);
        ESP_LOGI(TAG, "QR generated for: %s", uri);
    }
    else
    {
        ESP_LOGE(TAG, "QR generation failed");
    }
}

void HapQrScreen::update()
{
    Screen::update();

    if (m_update_count > 0)
    {
        m_update_count--;
        return;
    }

    if (m_refresh)
    {
        m_refresh      = false;
        m_update_count = 20;
        draw();
    }
}

void HapQrScreen::receiveEvent(evt_t* evt)
{
    if (evt->type == EVT_BTN_PRESS)
        m_closed = true;
}

void HapQrScreen::draw()
{
    m_display.clear();
    m_display.setColor(WHITE);

    if (m_state == State::Paired)
    {
        m_display.setFont(ArialMT_Plain_16);
        m_display.drawString(8, 16, "HomeKit");
        m_display.drawString(8, 36, "Paired!");
        m_display.display();
        return;
    }

    // State::QR
    if (m_qr_code)
    {
        int size = qrcodegen_getSize(m_qr_code.get());
        for (int py = 0; py < 64; py++)
        {
            for (int px = 0; px < 64; px++)
            {
                if (qrcodegen_getModule(m_qr_code.get(), (px * size) / 64, (py * size) / 64))
                    m_display.setPixel(px, py);
            }
        }
    }
    else
    {
        m_display.setFont(ArialMT_Plain_10);
        m_display.drawString(2, 20, "No setup");
        m_display.drawString(2, 32, "code set");
    }

    m_display.setFont(ArialMT_Plain_10);
    m_display.drawString(66, 2,  "HomeKit");
    m_display.drawString(66, 16, "Scan in");
    m_display.drawString(66, 28, "Home app");
    m_display.drawBitmap(66, 46, cancel_icon16x16, 16, 16);
    m_display.drawString(84, 50, "=Exit");

    m_display.display();
}
