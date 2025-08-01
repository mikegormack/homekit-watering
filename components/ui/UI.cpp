
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/event_groups.h>
#include <freertos/queue.h>

#include <esp_log.h>
#include <esp_timer.h>

#include <HomeScreen.h>
#include <SettingsMenu.h>

#include <ctime>
#include <sstream>
#include <iomanip>
#include <memory>
#include <mutex>

#include <SSD1306I2C.h>

#include <iocfg.h>

#include <UI.h>

static const char *TAG = "UI";

#define EVT_QUEUE_SIZE 8

#define BTN_MAX_DOWN_TIME (60000)

#define BTN_SHORT_PRESS_MS (25)
#define BTN_LONG_PRESS_MS (2000)
#define BTN_VLONG_PRESS_MS (5000)

#define BTN_POLL_TIME_MS (25)

#define BTN_REPEAT_TIME_MS (100)
#define BTN_REPEAT_CYCLES (BTN_REPEAT_TIME_MS / BTN_POLL_TIME_MS)

#define UI_TASK_PERIOD_MS (10)

#define MENU_TIMEOUT_MS (10000)

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
	const uint16_t io_mask;
	btn_state_t state;
	uint64_t down_tick;
	uint8_t rpt_count;
} btn_t;

static btn_t buttons[] =
	{
		{.id = BTN_SEL_ID, .io_mask = BTN_SEL_IOEXP_MASK, .state = BTN_STATE_UP, .down_tick = 0, .rpt_count = 0},
		{.id = BTN_BACK_ID, .io_mask = BTN_BACK_IOEXP_MASK, .state = BTN_STATE_UP, .down_tick = 0, .rpt_count = 0},
		{.id = BTN_UP_ID, .io_mask = BTN_UP_IOEXP_MASK, .state = BTN_STATE_UP, .down_tick = 0, .rpt_count = 0},
		{.id = BTN_DN_ID, .io_mask = BTN_DN_IOEXP_MASK, .state = BTN_STATE_UP, .down_tick = 0, .rpt_count = 0}};

#define NUM_BUTTONS ARRAY_SIZE(buttons)

UI::UI(SSD1306I2C &display, std::shared_ptr<MCP23017> io_exp, MenuCtx& menu_ctx) :
	m_display(display),
	m_io_exp(io_exp),
	m_menu_ctx(menu_ctx)
{
	m_io_exp->setEventCallback(io_int_callback, this);
	m_current_scr = std::make_unique<HomeScreen>(display, 0);

	const esp_timer_create_args_t timer_args = {
		.callback = &button_tmr_handler,
		.arg = this,
		.dispatch_method = ESP_TIMER_TASK,
		.name = "single_shot",
		.skip_unhandled_events = true,
	};
	esp_err_t res = esp_timer_create(&timer_args, &m_btn_timer);
	if (res != ESP_OK)
	{
		ESP_LOGE(TAG, "timer create fail %d", res);
	}
	m_evt_queue = xQueueCreate(EVT_QUEUE_SIZE, sizeof(evt_t));
	if (m_evt_queue == nullptr)
	{
		ESP_LOGE(TAG, "queue create fail %d", res);
	}
	BaseType_t result = xTaskCreate(ui_thread_entry, "UI Task", 4096, this, 1, NULL);
	if (result == pdFAIL)
	{
		ESP_LOGE(TAG, "task alloc fail");
	}
}

UI::~UI()
{
	if (m_evt_queue != nullptr)
		vQueueDelete(m_evt_queue);

	esp_timer_delete(m_btn_timer);
}

void UI::ui_thread_entry(void *p)
{
	ESP_LOGI(TAG, "Started");
	UI *ctx = (UI *)p;
	evt_t evt;
	while (1)
	{
		vTaskDelay(pdMS_TO_TICKS(UI_TASK_PERIOD_MS));
		if (ctx->m_current_scr != nullptr)
		{
			ctx->m_current_scr->update();
			if (ctx->m_current_scr->isClosed())
			{
				ctx->m_current_scr = std::make_unique<HomeScreen>(ctx->m_display, 0);
				ctx->m_menu_active = false;
			}
		}
		while (xQueueReceive(ctx->m_evt_queue, &evt, pdMS_TO_TICKS(5)))
		{
			ESP_LOGI(TAG, "evt %d %d", evt.id, evt.type);
			if (ctx->m_menu_active)
			{
				ctx->m_current_scr->receiveEvent(&evt);
			}
			else
			{
				if (evt.id == BTN_SEL_ID && evt.type == EVT_BTN_HOLD)
				{
					ctx->m_current_scr = std::make_unique<SettingsMenu>(ctx->m_display, (MENU_TIMEOUT_MS / UI_TASK_PERIOD_MS), ctx->m_menu_ctx);
					ctx->m_menu_active = true;
				}
			}
		}
	}
}

void UI::io_int_callback(uint16_t flags, uint16_t capture, void *user_data)
{
	UI *ctx = (UI *)user_data;
	ESP_LOGI(TAG, "Int cb %d %d", flags, capture);
	ctx->process_buttons();
}

void UI::button_tmr_handler(void *arg)
{
	if (arg != nullptr)
	{
		UI *it = (UI *)arg;
		it->process_buttons();
	}
}

void UI::process_buttons()
{
	std::lock_guard<std::mutex> lock(m_btn_mutex);
	uint16_t pin = 0;
	bool newState[NUM_BUTTONS];
	bool run_timer = false;

	if (m_io_exp->getGPIO(&pin) == false)
	{
		ESP_LOGE(TAG, "Failed to read IOEXP");
		return;
	}
	for (int i = 0; i < NUM_BUTTONS; i++)
	{
		newState[i] = (pin & buttons[i].io_mask) == 0;
	}

	ESP_LOGI(TAG, "pin = %04x", pin);

	for (int i = 0; i < NUM_BUTTONS; i++)
	{
		if (buttons[i].state == BTN_STATE_UP)
		{
			if (newState[i])
			{
				run_timer = true;
				buttons[i].down_tick = esp_timer_get_time();
				buttons[i].state = BTN_STATE_DOWN;
				// ESP_LOGI(TAG, "Btn %d dn tick %lld", i, buttons[i].down_tick);
			}
		}
		else
		{
			// ESP_LOGI(TAG, "Btn %d dn tick %lld", i, esp_timer_get_time());
			uint32_t ms = ((uint32_t)(esp_timer_get_time() - buttons[i].down_tick)) / 1000;
			if (ms < BTN_MAX_DOWN_TIME)
			{
				run_timer = true;
				if (newState[i])
				{
					if ((buttons[i].state == BTN_STATE_DOWN) && (ms > BTN_LONG_PRESS_MS))
					{
						buttons[i].state = BTN_STATE_LONG_HOLD;
						buttons[i].rpt_count = 0;
						ESP_LOGI(TAG, "******** Btn %d long hold", i);
						evt_t evt = {.id = buttons[i].id, .type = EVT_BTN_HOLD};
						xQueueSend(m_evt_queue, &evt, 0);
					}
					else if ((buttons[i].state == BTN_STATE_LONG_HOLD) && (ms > BTN_VLONG_PRESS_MS))
					{
						buttons[i].state = BTN_STATE_VLONG_HOLD;
						ESP_LOGI(TAG, "********** Btn %d v long hold", i);
						evt_t evt = {.id = buttons[i].id, .type = EVT_BTN_LONG_HOLD};
						xQueueSend(m_evt_queue, &evt, 0);
					}
					else if ((buttons[i].state == BTN_STATE_LONG_HOLD) || (buttons[i].state == BTN_STATE_VLONG_HOLD))
					{
						buttons[i].rpt_count++;
						if (buttons[i].rpt_count == BTN_REPEAT_CYCLES)
						{
							ESP_LOGI(TAG, "******** Btn %d hold rpt", i);
							evt_t evt = {.id = buttons[i].id, .type = EVT_BTN_HOLD_RPT};
							xQueueSend(m_evt_queue, &evt, 0);
							buttons[i].rpt_count = 0;
						}
					}
				}
				else
				{
					if ((buttons[i].state == BTN_STATE_DOWN) && (ms > BTN_SHORT_PRESS_MS))
					{
						buttons[i].state = BTN_STATE_PRESS;
						ESP_LOGI(TAG, "********** Btn %d rls %lu", i, ms);
						evt_t evt = {.id = buttons[i].id, .type = EVT_BTN_PRESS};
						xQueueSend(m_evt_queue, &evt, 0);
					}
					buttons[i].state = BTN_STATE_UP;
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
	}
}
