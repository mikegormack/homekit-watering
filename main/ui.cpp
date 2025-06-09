
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/event_groups.h>

#include <esp_log.h>
#include <esp_timer.h>

#include <ctime>
#include <sstream>
#include <iomanip>
#include <memory>
#include <mutex>

#include <SSD1306I2C.h>

#include <iocfg.h>

#include <ui.h>

static const char *TAG = "UI";

#define NUM_BUTTONS                         2
#define BTN_MAX_DOWN_TIME                   (60000)

#define BTN_SHORT_PRESS_MS                  (100)
#define BTN_LONG_PRESS_MS                   (3000)
#define BTN_VLONG_PRESS_MS                  (10000)

#define BTN_POLL_TIME_MS                    (50)

typedef enum
{
	BTN_STATE_UP,
	BTN_STATE_DOWN,
	BTN_STATE_PRESS,
	BTN_STATE_LONG_HOLD,
	BTN_STATE_VLONG_HOLD,
	BTN_STATE_MAX_HOLD,
} btn_state_t;

typedef struct
{
	const uint8_t id;
	btn_state_t state;
	uint64_t down_tick;
} btn_t;

static btn_t buttons[NUM_BUTTONS] =
{
	{ .id = 1, .state = BTN_STATE_UP, .down_tick = 0 },
	{ .id = 2, .state = BTN_STATE_UP, .down_tick = 0 }
};



ui::ui(SSD1306I2C& display, std::shared_ptr<MCP23017> io_exp) :
	m_display(display),
	m_io_exp(io_exp),
	m_home_screen(display)
{
	m_io_exp->setEventCallback(io_int_callback, this);
	m_event_group = xEventGroupCreate();
	xTaskCreate(ui_thread_entry, "UI Task", 4096, this, 1, NULL);

	const esp_timer_create_args_t timer_args = {
        .callback = &button_tmr_handler,
        .arg = this,
        .dispatch_method = ESP_TIMER_TASK,
        .name = "single_shot",
        .skip_unhandled_events = true,
    };

    esp_timer_create(&timer_args, &m_btn_timer);
}

ui::~ui()
{

}

void ui::ui_thread_entry(void *p)
{
	ESP_LOGI(TAG, "Started");
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

void ui::button_tmr_handler(void* arg)
{
	if (arg != nullptr)
	{
		ui* it = (ui*)arg;
		it->process_buttons();
	}
}

void ui::process_buttons()
{
	std::lock_guard<std::mutex> lock(m_btn_mutex);
	uint16_t pin = 0;
	bool newState[NUM_BUTTONS] = {false, false};
	bool run_timer = false;
	ESP_LOGI(TAG, "Process buttons");

	if (m_io_exp->getGPIO(&pin) == false)
	{
		ESP_LOGE(TAG, "Failed to read IOEXP");
		return;
	}
	newState[0] = (pin & BTN_1_IOEXP_MASK) == 0;
	newState[1] = (pin & BTN_2_IOEXP_MASK) == 0;
	//newState[2] = (pin & BTN_3_IOEXP_MASK) == 0;
	//newState[3] = (pin & BTN_4_IOEXP_MASK) == 0;

	ESP_LOGI(TAG, "pin = %04x", pin);

	for (uint8_t i = 0; i < NUM_BUTTONS; i++)
	{
		if (buttons[i].state == BTN_STATE_UP)
		{
			if (newState[i])
			{
				run_timer = true;
				buttons[i].down_tick = esp_timer_get_time();
				buttons[i].state = BTN_STATE_DOWN;

				ESP_LOGI(TAG, "Btn %d dn tick %lld", i, buttons[i].down_tick);
				/*if (btnCallback != NULL)
					btnCallback(&buttons[i]);*/
			}
		}
		else
		{
			ESP_LOGI(TAG, "Btn %d up tick %lld", i, esp_timer_get_time());
			uint32_t ms = ((uint32_t)(esp_timer_get_time() - buttons[i].down_tick)) / 1000;
			if (ms < BTN_MAX_DOWN_TIME)
			{
				run_timer = true;
				if (newState[i])
				{
					if ((buttons[i].state == BTN_STATE_DOWN) && (ms > BTN_LONG_PRESS_MS))
					{
						buttons[i].state = BTN_STATE_LONG_HOLD;
						ESP_LOGI(TAG, "Btn %d long hold", i);
						/*if (btnCallback != NULL)
							btnCallback(&buttons[i]);*/
					}
					else if ((buttons[i].state == BTN_STATE_LONG_HOLD) && (ms > BTN_VLONG_PRESS_MS))
					{
						buttons[i].state = BTN_STATE_VLONG_HOLD;
						ESP_LOGI(TAG, "Btn %d v long hold", i);
						/*if (btnCallback != NULL)
							btnCallback(&buttons[i]);*/
					}
				}
				else
				{
					if ((buttons[i].state == BTN_STATE_DOWN) && (ms > BTN_SHORT_PRESS_MS))
					{
						buttons[i].state = BTN_STATE_PRESS;
						ESP_LOGI(TAG, "Btn %d rls %lu", i, ms);
						/*if (btnCallback != NULL)
							btnCallback(&buttons[i]);*/
					}
					buttons[i].state = BTN_STATE_UP;
					/*if (btnCallback != NULL)
						btnCallback(&buttons[i]);*/
				}
			}
			else
			{
				buttons[i].state = BTN_STATE_MAX_HOLD;
				if (newState[i])
				{
					buttons[i].state = BTN_STATE_UP;
				}
			}
		}
	}
	if (run_timer)
	{
		esp_timer_start_once(m_btn_timer, BTN_POLL_TIME_MS * 1000);
		// while any button is pressed run the timer to poll
		//sl_simple_timer_start(&uiBtnTimer, BTN_POLL_TIME_MS, BtnTimerEvent, NULL, false);
	}
}
/*
static void processBtn(void)
{
	uint16_t val = 0;
	bool newState[NUM_BUTTONS];

	// if in shutdown mode ignore buttons
	if (driverShutdown)
		return;

	if (drvTca9535_GetInput(IO_EXP_HW_ADDR, &val))
	{
		newState[0] = ((val & (1 << UI_BUTTON0_IOEXP_PIN)) == (1 << UI_BUTTON0_IOEXP_PIN)) ? BTN_RELEASED : BTN_PRESSED;
		newState[1] = ((val & (1 << UI_BUTTON1_IOEXP_PIN)) == (1 << UI_BUTTON1_IOEXP_PIN)) ? BTN_RELEASED : BTN_PRESSED;

		bool run_timer = false;
		for (uint8_t i = 0; i < NUM_BUTTONS; i++)
		{
			if (buttons[i].state == BTN_STATE_UP)
			{
				if (newState[i] == BTN_PRESSED)
				{
					DebugPrintF(DBG_PRINT_UI, "Btn %d dn", i);
					run_timer = true;
					buttons[i].down_tick = sl_sleeptimer_get_tick_count64();
					buttons[i].state = BTN_STATE_DOWN;
					if (btnCallback != NULL)
						btnCallback(&buttons[i]);
				}
			}
			else
			{
				uint32_t ms = sl_sleeptimer_tick_to_ms((uint32_t)(sl_sleeptimer_get_tick_count64() - buttons[i].down_tick));
				if (ms < BTN_MAX_DOWN_TIME)
				{
					run_timer = true;
					if (newState[i] == BTN_PRESSED)
					{
						if ((buttons[i].state == BTN_STATE_DOWN) && (ms > BTN_LONG_PRESS_MS))
						{
							buttons[i].state = BTN_STATE_LONG_HOLD;
							DebugPrintF(DBG_PRINT_UI, "Btn %d long hold", i);
							if (btnCallback != NULL)
								btnCallback(&buttons[i]);
						}
						else if ((buttons[i].state == BTN_STATE_LONG_HOLD) && (ms > BTN_VLONG_PRESS_MS))
						{
							buttons[i].state = BTN_STATE_VLONG_HOLD;
							DebugPrintF(DBG_PRINT_UI, "Btn %d v long hold", i);
							if (btnCallback != NULL)
								btnCallback(&buttons[i]);
						}
					}
					else
					{
						if ((buttons[i].state == BTN_STATE_DOWN) && (ms > BTN_SHORT_PRESS_MS))
						{
							buttons[i].state = BTN_STATE_PRESS;
							DebugPrintF(DBG_PRINT_UI, "Btn %d rls %u", i, ms);
							if (btnCallback != NULL)
								btnCallback(&buttons[i]);
						}
						buttons[i].state = BTN_STATE_UP;
						if (btnCallback != NULL)
							btnCallback(&buttons[i]);
					}
				}
				else
				{
					buttons[i].state = BTN_STATE_MAX_HOLD;
					if (newState[i] == BTN_RELEASED)
					{
						buttons[i].state = BTN_STATE_UP;
					}
				}
			}
		}

		if (run_timer)
		{
			// while any button is pressed run the timer to poll
			sl_simple_timer_start(&uiBtnTimer, BTN_POLL_TIME_MS, BtnTimerEvent, NULL, false);
		}
	}
}*/

void ui::io_int_callback(uint16_t flags, uint16_t capture, void* user_data)
{
	ui* ctx = (ui*)user_data;
	ESP_LOGI(TAG, "Int cb %d %d", flags, capture);
	xEventGroupSetBits(ctx->m_event_group, 1);
}


