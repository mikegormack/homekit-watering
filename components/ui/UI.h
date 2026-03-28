#pragma once
#include <SSD1306I2C.h>
#include <MCP23017.h>

#include <freertos/event_groups.h>
#include <freertos/queue.h>

#include <esp_timer.h>

#include <Screen.h>
#include <HomeScreen.h>
#include <MenuCtx.h>

#include <memory>
#include <mutex>

class UI
{
public:
	UI(SSD1306I2C &display, std::shared_ptr<MCP23017> io_exp, MenuCtx& menu_ctx);
	~UI();

	std::unique_ptr<Screen> m_current_scr;

private:
	enum btn_state_t
	{
		BTN_STATE_UP,
		BTN_STATE_DOWN,
		BTN_STATE_PRESS,
		BTN_STATE_LONG_HOLD,
		BTN_STATE_VLONG_HOLD,
		BTN_STATE_MAX_HOLD,
	};

	struct btn_t
	{
		const uint8_t  id;
		const uint16_t io_mask;
		btn_state_t    state;
		uint64_t       down_tick;
		uint8_t        rpt_count;
	};

	static constexpr int NUM_BUTTONS = 6;

	SSD1306I2C& m_display;
	std::shared_ptr<MCP23017> m_io_exp;
	MenuCtx& m_menu_ctx;
	esp_timer_handle_t m_btn_timer;
	bool m_menu_active = false;

	std::mutex m_btn_mutex;
	btn_t      m_buttons[NUM_BUTTONS];

	QueueHandle_t m_evt_queue = NULL;

	static void ui_thread_entry(void *p);
	static void io_int_callback(uint16_t flags, uint16_t capture, void *user_data);
	static void button_tmr_handler(void *arg);

	void process_buttons();
};
