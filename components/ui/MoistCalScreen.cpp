
#include <MoistCalScreen.h>
#include <SSD1306I2C.h>
#include <iocfg.h>

#include <stdio.h>

MoistCalScreen::MoistCalScreen(SSD1306I2C& display, uint32_t timeout_tick, MenuCtx& ctx)
    : Screen(display, timeout_tick), m_ctx(ctx)
{
}

void MoistCalScreen::open()
{
    m_state        = State::DRY_PROMPT;
    m_sample_count = 0;
    m_sample_sum   = 0;
    m_raw_dry      = 0;
    m_raw_wet      = 0;
    Screen::open();
}

void MoistCalScreen::update()
{
    Screen::update();
    if (m_closed)
        return;

    if (m_state == State::DRY_SAMPLING || m_state == State::WET_SAMPLING)
    {
        // Take one sample per tick
        int raw = m_ctx.moisture.raw_getter ? m_ctx.moisture.raw_getter() : 0;
        if (raw >= 0)
        {
            m_sample_sum += raw;
            m_sample_count++;
        }

        if (m_sample_count >= SAMPLE_COUNT)
        {
            int avg = (int)(m_sample_sum / m_sample_count);
            if (m_state == State::DRY_SAMPLING)
            {
                m_raw_dry      = avg;
                m_state        = State::WET_PROMPT;
            }
            else
            {
                m_raw_wet = avg;
                if (m_raw_wet == m_raw_dry)
                {
                    m_state = State::ERROR;
                }
                else
                {
                    if (m_ctx.moisture.cal_setter)
                        m_ctx.moisture.cal_setter(m_raw_dry, m_raw_wet);
                    m_state = State::DONE;
                }
            }
            m_refresh = true;
        }
        else if ((m_sample_count % 4) == 0)
        {
            m_refresh = true; // update progress every 4 samples
        }
    }

    if (m_refresh)
    {
        draw();
        m_refresh = false;
    }
}

void MoistCalScreen::receiveEvent(evt_t* evt)
{
    if (evt->type != EVT_BTN_PRESS)
        return;

    if (evt->id == BTN_BACK_ID)
    {
        m_closed = true;
        return;
    }

    if (evt->id == BTN_SEL_ID)
    {
        if (m_state == State::DRY_PROMPT)
        {
            m_sample_count = 0;
            m_sample_sum   = 0;
            m_state        = State::DRY_SAMPLING;
            m_refresh      = true;
        }
        else if (m_state == State::WET_PROMPT)
        {
            m_sample_count = 0;
            m_sample_sum   = 0;
            m_state        = State::WET_SAMPLING;
            m_refresh      = true;
        }
        else if (m_state == State::DONE || m_state == State::ERROR)
        {
            m_closed = true;
        }
    }

    refreshTimeout();
}

void MoistCalScreen::draw()
{
    m_display.clear();
    m_display.setColor(WHITE);

    switch (m_state)
    {
        case State::DRY_PROMPT:
        {
            int raw = m_ctx.moisture.raw_getter ? m_ctx.moisture.raw_getter() : 0;
            drawPrompt("Dry Cal", "Keep sensor DRY", raw);
            break;
        }
        case State::DRY_SAMPLING:
            drawSampling();
            break;
        case State::WET_PROMPT:
        {
            int raw = m_ctx.moisture.raw_getter ? m_ctx.moisture.raw_getter() : 0;
            drawPrompt("Wet Cal", "Submerge sensor", raw);
            break;
        }
        case State::WET_SAMPLING:
            drawSampling();
            break;
        case State::DONE:
            drawDone();
            break;
        case State::ERROR:
            drawError();
            break;
    }

    m_display.display();
}

void MoistCalScreen::drawPrompt(const char* title, const char* instruction, int raw)
{
    m_display.setFont(ArialMT_Plain_10);
    m_display.drawString(0, 0, title);
    m_display.drawHorizontalLine(0, 12, 128);
    m_display.drawString(0, 16, instruction);

    char buf[24];
    snprintf(buf, sizeof(buf), "Raw: %d", raw);
    m_display.drawString(0, 30, buf);
    m_display.drawString(0, 44, "SEL to sample");
    m_display.drawString(0, 54, "BACK to cancel");
}

void MoistCalScreen::drawSampling()
{
    m_display.setFont(ArialMT_Plain_10);
    m_display.drawString(0, 0, "Sampling...");
    m_display.drawHorizontalLine(0, 12, 128);

    uint8_t pct = (uint8_t)((m_sample_count * 100) / SAMPLE_COUNT);
    m_display.drawProgressBar(0, 28, 128, 12, pct);

    char buf[16];
    snprintf(buf, sizeof(buf), "%d / %d", m_sample_count, SAMPLE_COUNT);
    m_display.drawString(44, 44, buf);
}

void MoistCalScreen::drawDone()
{
    m_display.setFont(ArialMT_Plain_10);
    m_display.drawString(0, 0, "Cal Saved!");
    m_display.drawHorizontalLine(0, 12, 128);

    char buf[32];
    snprintf(buf, sizeof(buf), "Dry: %d", m_raw_dry);
    m_display.drawString(0, 16, buf);
    snprintf(buf, sizeof(buf), "Wet: %d", m_raw_wet);
    m_display.drawString(0, 28, buf);
    m_display.drawString(0, 44, "SEL or BACK to exit");
}

void MoistCalScreen::drawError()
{
    m_display.setFont(ArialMT_Plain_10);
    m_display.drawString(0, 0, "Cal Error");
    m_display.drawHorizontalLine(0, 12, 128);
    m_display.drawString(0, 16, "Dry = Wet reading");
    m_display.drawString(0, 28, "Check sensor wiring");
    m_display.drawString(0, 44, "SEL or BACK to exit");
}
