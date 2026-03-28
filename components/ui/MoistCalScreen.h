#pragma once

#include <Screen.h>
#include <MenuCtx.h>

class MoistCalScreen : public Screen
{
public:
    MoistCalScreen(SSD1306I2C& display, uint32_t timeout_tick, MenuCtx& ctx);
    ~MoistCalScreen() = default;

    void update() override;
    void receiveEvent(evt_t* evt) override;
    void open() override;

private:
    enum class State { DRY_PROMPT, DRY_SAMPLING, WET_PROMPT, WET_SAMPLING, DONE, ERROR };

    MenuCtx& m_ctx;
    State    m_state        = State::DRY_PROMPT;
    int      m_sample_count = 0;
    long     m_sample_sum   = 0;
    int      m_raw_dry      = 0;
    int      m_raw_wet      = 0;

    static constexpr int SAMPLE_COUNT = 32;

    void draw();
    void drawPrompt(const char* title, const char* instruction, int raw);
    void drawSampling();
    void drawDone();
    void drawError();
};
