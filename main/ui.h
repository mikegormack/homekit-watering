#pragma once
#include <SSD1306I2C.h>
#include <MCP23017.h>

#include <freertos/event_groups.h>

#include <esp_timer.h>

#include <homeScreen.h>

#include <memory>


class ui
{
private:
	SSD1306I2C& m_display;
	std::shared_ptr<MCP23017> m_io_exp;
	EventGroupHandle_t m_event_group;

	esp_timer_handle_t m_btn_timer;

	std::mutex m_btn_mutex;

	static void io_int_callback(uint16_t flags, uint16_t capture, void* user_data);

	void process_buttons();

	static void	button_tmr_handler(void* arg);

public:
	ui(SSD1306I2C& display, std::shared_ptr<MCP23017> io_exp);
	~ui();

	static void ui_thread_entry(void *p);

	homeScreen m_home_screen;
};
