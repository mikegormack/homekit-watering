#pragma once
#include <SSD1306I2C.h>
#include <MCP23017.h>

#include <freertos/event_groups.h>
#include <freertos/queue.h>

#include <esp_timer.h>

#include <Screen.h>
#include <HomeScreen.h>

#include <memory>
#include <mutex>

class UI
{
public:
	UI(SSD1306I2C &display, std::shared_ptr<MCP23017> io_exp);
	~UI();

	std::unique_ptr<Screen> m_current_scr;

private:
	SSD1306I2C &m_display;
	std::shared_ptr<MCP23017> m_io_exp;
	esp_timer_handle_t m_btn_timer;

	std::mutex m_btn_mutex;

	QueueHandle_t m_evt_queue = NULL;

	static void ui_thread_entry(void *p);

	static void io_int_callback(uint16_t flags, uint16_t capture, void *user_data);

	static void button_tmr_handler(void *arg);

	void process_buttons();
};
