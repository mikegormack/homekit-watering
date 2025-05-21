
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/event_groups.h>

#include <esp_log.h>

#include <ctime>
#include <sstream>
#include <iomanip>
#include <memory>

#include <SSD1306I2C.h>

#include <ui.h>

static const char *TAG = "UI";

ui::ui(SSD1306I2C& display, std::shared_ptr<MCP23017> io_exp) :
	m_display(display),
	m_io_exp(io_exp),
	m_home_screen(display)
{
	m_io_exp->setEventCallback(io_int_callback, this);
	m_event_group = xEventGroupCreate();
	xTaskCreate(ui_thread_entry, "UI Task", 4096, this, 1, NULL);
}

ui::~ui()
{

}

void ui::ui_thread_entry(void *p)
{
	ui* ctx = (ui*)p;
	while (1)
	{
		EventBits_t bits = xEventGroupGetBits(ctx->m_event_group);
		if (bits & 1)
		{
			xEventGroupClearBits(ctx->m_event_group, 1);
			ctx->process_buttons();
		}
		vTaskDelay(500 / portTICK_PERIOD_MS);
		ctx->m_home_screen.update();
	}
}

void ui::process_buttons()
{
	ESP_LOGI(TAG, "Process buttons");
}

void ui::io_int_callback(uint16_t flags, uint16_t capture, void* user_data)
{
	ui* ctx = (ui*)user_data;
	ESP_LOGI(TAG, "Int cb %d %d", flags, capture);
	xEventGroupSetBits(ctx->m_event_group, 1);
}


