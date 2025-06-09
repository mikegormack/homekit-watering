
#include <driver/i2c_master.h>
#include <driver/gpio.h>

#include <esp_log.h>

#include <functional>

#include <freertos/freeRTOS.h>
#include <freertos/queue.h>

#include <MCP23017.h>

#define INTTERUPT_QUEUE_SIZE 10

static const char *TAG = "MCP23017";

MCP23017::MCP23017(i2c_master_bus_handle_t i2c_bus, uint8_t address, int res_pin, int inta_pin, int intb_pin, bool int_mirror) :
																													m_res_pin(res_pin),
																													m_int_mirror(int_mirror)
{
	if (inta_pin < -1 || inta_pin > 39)
	{
		ESP_LOGE(TAG, "invalid inta_pin val %d", inta_pin);
		inta_pin = -1;
	}
	if (inta_pin < -1 || inta_pin > 39)
	{
		ESP_LOGE(TAG, "invalid inta_pin val %d", inta_pin);
		inta_pin = -1;
	}
	if (intb_pin < -1 || intb_pin > 39)
	{
		ESP_LOGE(TAG, "invalid intb_pin val %d", intb_pin);
		intb_pin = -1;
	}
	m_inta_ctx.gpio = inta_pin;
	m_inta_ctx.drv = this;
	m_intb_ctx.gpio = intb_pin;
	m_intb_ctx.drv = this;

	i2c_device_config_t i2c_dev_conf =
	{
		.dev_addr_length = I2C_ADDR_BIT_LEN_7,
		.device_address = address,
		.scl_speed_hz = I2C_FREQ,
		.scl_wait_us = 0,
		.flags =
		{
			.disable_ack_check = 0
		}
	};

	esp_err_t ret = i2c_master_bus_add_device(i2c_bus, &i2c_dev_conf, &m_i2c_handle);
	if (ret != ESP_OK)
	{
		ESP_LOGE(TAG, "Failed to add i2c device %d", ret);
	}
	else
	{
		ESP_LOGI(TAG, "Added i2c device addr 0x%x", address);
	}
}

MCP23017::~MCP23017()
{
	if (m_int_evt_queue != nullptr)
		vQueueDelete(m_int_evt_queue);

	esp_err_t ret = i2c_master_bus_rm_device(m_i2c_handle);
	if (ret != ESP_OK)
	{
		ESP_LOGE(TAG, "Failed to remove i2c device %d", ret);
	}
	// exit task
}

bool MCP23017::init()
{
	bool success = true;
	esp_err_t ret = ESP_OK;

	reset();

	// config reset pin
	if (m_res_pin != -1)
	{
		gpio_config_t io_conf =
			{
				.pin_bit_mask = (1ULL << m_res_pin),
				.mode = GPIO_MODE_OUTPUT,
				.pull_up_en = GPIO_PULLUP_DISABLE,
				.pull_down_en = GPIO_PULLDOWN_DISABLE,
				.intr_type = GPIO_INTR_DISABLE};
		ret = gpio_config(&io_conf);
		if (ret != ESP_OK)
		{
			ESP_LOGE(TAG, "io res config fail %d", ret);
			success = false;
		}
	}
	// esp interrupt config
	if (m_inta_ctx.gpio != -1 || m_intb_ctx.gpio != -1)
	{
		gpio_install_isr_service(0);
		m_int_evt_queue = xQueueCreate(INTTERUPT_QUEUE_SIZE, sizeof(int));
		if (m_int_evt_queue == nullptr)
		{
			ESP_LOGE(TAG, "queue init fail");
			success = false;
		}
		else
		{
			BaseType_t result = xTaskCreate(intTask, "mcp23017_task", 4096, this, 10, NULL);
			if (result == pdFAIL)
			{
				ESP_LOGE(TAG, "task alloc fail");
				success = false;
			}
		}
	}
	if (m_inta_ctx.gpio != -1)
	{
		success = success && configIntPin(&m_inta_ctx);
	}
	if (m_intb_ctx.gpio != -1)
	{
		success = success && configIntPin(&m_intb_ctx);
	}
	uint8_t iocon = m_int_mirror ? IOCON_MIRROR : 0;
	success = success && writeRegister(MCP23017_IOCONA, iocon);

	ESP_LOGI(TAG, "init %s", success ? "ok" : "err");
	return success;
}

void MCP23017::uninit()
{
}

bool MCP23017::reset()
{
	esp_err_t ret = ESP_OK;
	if (m_res_pin == -1)
	{
	}

	gpio_num_t io_res_pin = (gpio_num_t)m_res_pin;
	ret = gpio_set_level(io_res_pin, 0);
	if (ret != ESP_OK)
		return false;
	vTaskDelay(pdMS_TO_TICKS(10));
	ret = gpio_set_level(io_res_pin, 1);
	vTaskDelay(pdMS_TO_TICKS(10));
	return (ret == ESP_OK);
}

void MCP23017::setEventCallback(std::function<void(uint16_t, uint16_t, void *)> callback, void *user_data)
{
	m_callback = callback;
	m_user_data = user_data;
}

bool MCP23017::configIntPin(struct mcp23017_int_ctx_s *ctx)
{
	bool success = true;
	esp_err_t ret = ESP_OK;
	gpio_config_t io_conf =
		{
			.pin_bit_mask = (1ULL << ctx->gpio),
			.mode = GPIO_MODE_INPUT,
			.pull_up_en = GPIO_PULLUP_DISABLE,
			.pull_down_en = GPIO_PULLDOWN_DISABLE,
			.intr_type = GPIO_INTR_NEGEDGE};
	ret = gpio_config(&io_conf);
	if (ret != ESP_OK)
	{
		ESP_LOGE(TAG, "int (%d) config fail %d", ctx->gpio, ret);
		success = false;
	}
	ret = gpio_isr_handler_add((gpio_num_t)ctx->gpio, isrHandler, ctx);
	if (ret != ESP_OK)
	{
		ESP_LOGE(TAG, "int (%d) handler fail %d", ctx->gpio, ret);
		success = false;
	}
	return success;
}

void IRAM_ATTR MCP23017::isrHandler(void *arg)
{
	mcp23017_int_ctx_s *ctx = static_cast<mcp23017_int_ctx_s *>(arg);
	xQueueSendFromISR(ctx->drv->m_int_evt_queue, &ctx->gpio, NULL);
}

void MCP23017::intTask(void *arg)
{
	int gpio_num = 0;
	MCP23017 *ctx = (MCP23017 *)arg;
	while (1)
	{
		/*uint8_t iocon =IOCON_MIRROR;
		if (ctx->writeRegister(MCP23017_IOCONA, iocon))
		{
			ESP_LOGI(TAG, "writeRegister ok");
		}*/


		/*if (xQueueReceive(ctx->m_int_evt_queue, &gpio_num, portMAX_DELAY))
		{
			vTaskDelay(pdMS_TO_TICKS(2));
		}
		else
		{
			gpio_num = 0;
		}
		if (gpio_num > 0)
		{
			uint16_t flags = 0;
			uint16_t cap = 0;
			if (ctx->getIntFlag(&flags) && ctx->getIntCapture(&cap))
			{
				gpio_num = 0;
				// read capture reg resets interrupt
				if (ctx->m_callback != nullptr)
					ctx->m_callback(flags, cap, ctx->m_user_data);

				ESP_LOGI(TAG, "Event cb");
			}
		}
		vTaskDelay(pdMS_TO_TICKS(5));*/

		vTaskDelay(pdMS_TO_TICKS(2000));
	}
}

bool MCP23017::setIODIR(uint16_t iodir)
{
	return writeRegister16(MCP23017_IODIRA, iodir);
}

bool MCP23017::getIODIR(uint16_t *iodir)
{
	return readRegister16(MCP23017_IODIRA, iodir);
}

bool MCP23017::setGPIO(uint16_t gpio)
{
	return writeRegister16(MCP23017_OLATA, gpio);
}

bool MCP23017::getGPIO(uint16_t *gpio)
{
	return readRegister16(MCP23017_GPIOA, gpio);
}

bool MCP23017::setPullUp(uint16_t gpio)
{
	return writeRegister16(MCP23017_GPPUA, gpio);
}

bool MCP23017::getPullUp(uint16_t *gpio)
{
	return readRegister16(MCP23017_GPPUA, gpio);
}

bool MCP23017::setIntEna(uint16_t mask)
{
	return writeRegister16(MCP23017_GPINTENA, mask);
}

bool MCP23017::getIntEna(uint16_t *mask)
{
	return readRegister16(MCP23017_GPINTENA, mask);
}

bool MCP23017::setIntDefaultEnable(uint16_t enable)
{
	return writeRegister16(MCP23017_INTCONA, enable);
}

bool MCP23017::getIntDefaultEnable(uint16_t *enable)
{
	return readRegister16(MCP23017_INTCONA, enable);
}

bool MCP23017::setIntDefaultValue(uint16_t val)
{
	return writeRegister16(MCP23017_DEFVALA, val);
}

bool MCP23017::getIntDefaultValue(uint16_t *val)
{
	return readRegister16(MCP23017_DEFVALA, val);
}

bool MCP23017::getIntFlag(uint16_t *val)
{
	return readRegister16(MCP23017_INTFA, val);
}

bool MCP23017::getIntCapture(uint16_t *val)
{
	return readRegister16(MCP23017_INTCAPA, val);
}

void MCP23017::regDump()
{
	uint8_t temp = 0;
	uint8_t i = 0;
	for (i = 0x00; i <= 0x15; i++)
	{
		if (readRegister(i, &temp))
		{
			ESP_LOGI(TAG, "reg %x: %x", i, temp);
		}
		else
		{
			ESP_LOGI(TAG, "reg %x: fail", i);
		}
	}
}

bool MCP23017::setIntPinConfig(bool mirror, bool open_drain, bool act_hi)
{
	uint8_t iocon = 0;
	if (mirror)
		iocon |= IOCON_MIRROR;
	if (open_drain)
		iocon |= IOCON_ODR;
	if (act_hi)
		iocon |= IOCON_INTPOL;
	return writeRegister(MCP23017_IOCONA, 0);
}

bool MCP23017::readRegister(uint8_t reg_addr, uint8_t *val)
{
	esp_err_t ret = i2c_master_transmit_receive(m_i2c_handle, &reg_addr, sizeof(reg_addr), val, 1, pdMS_TO_TICKS(100));
	if (ret != ESP_OK)
	{
		ESP_LOGE(TAG, "Read reg error: %d", ret);
		return false;
	}
	else
	{
		return true;
	}
}

bool MCP23017::writeRegister(uint8_t reg_addr, uint8_t val)
{
	uint8_t data[2] = {reg_addr, val};
	esp_err_t ret = i2c_master_transmit(m_i2c_handle, data, sizeof(data), pdMS_TO_TICKS(100));
	if (ret != ESP_OK)
	{
		ESP_LOGE(TAG, "Write reg error: %d", ret);
		return false;
	}
	else
	{
		return true;
	}
}

bool MCP23017::readRegister16(uint8_t reg_addr, uint16_t *val)
{
	uint8_t data[2] = {0};

	esp_err_t ret = i2c_master_transmit_receive(m_i2c_handle, &reg_addr, sizeof(reg_addr), data, sizeof(data), pdMS_TO_TICKS(100));
	if (ret != ESP_OK)
	{
		ESP_LOGE(TAG, "Read reg error: %d", ret);
		return false;
	}
	else
	{

		*val = data[0];
		*val |= (data[1] << 8);
		ESP_LOGI(TAG, "Read reg: 0x%02x val 0x%04x", reg_addr, *val);
		return true;
	}
}

bool MCP23017::writeRegister16(uint8_t reg_addr, uint16_t val)
{
	uint8_t data[3] = {reg_addr, (uint8_t)(val & 0xFF), (uint8_t)((val >> 8) & 0xFF)};
	esp_err_t ret = i2c_master_transmit(m_i2c_handle, data, sizeof(data), pdMS_TO_TICKS(10));
	if (ret != ESP_OK)
	{
		ESP_LOGE(TAG, "Write reg16 error: %d", ret);
		return false;
	}
	else
	{
		return true;
	}
}
