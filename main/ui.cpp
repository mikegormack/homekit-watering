
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include <ctime>
#include <sstream>
#include <iomanip>

#include <SSD1306I2C.h>

#include <ui.h>

ui::ui(SSD1306I2C& display) :
	m_display(display),
	m_home_screen(display)
{
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
		vTaskDelay(500 / portTICK_PERIOD_MS);
		ctx->m_home_screen.update();
	}
}


