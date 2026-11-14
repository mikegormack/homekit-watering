#include <string.h>

#include <esp_log.h>
#include <esp_rom_sys.h>
#include <esp_timer.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include <StemmaSoil.h>

static const char *TAG = "StemmaSoil";

#define STEMMA_I2C_FREQ        50000   // slow and forgiving: we only move a few bytes
#define STEMMA_I2C_TIMEOUT_MS  50

// How long the sensor may hold SCL low (clock stretching). The seesaw chip stretches
// while it works; the ESP32 default of 2 ms can cut it off, which leaves the I2C
// hardware stuck and can flood the CPU with interrupts (interrupt watchdog reboot).
// ESP32 hardware caps this at about 13 ms.
#define STEMMA_SCL_WAIT_US     10000

// seesaw module bases and registers
#define SEESAW_STATUS_BASE     0x00
#define SEESAW_STATUS_HW_ID    0x01
#define SEESAW_STATUS_VERSION  0x02
#define SEESAW_STATUS_TEMP     0x04
#define SEESAW_STATUS_SWRST    0x7F

#define SEESAW_TOUCH_BASE      0x0F
#define SEESAW_TOUCH_CH_OFFSET 0x10

// Known seesaw hardware IDs (SAMD09/SAMD10 boards report 0x55)
#define SEESAW_HW_ID_SAMD09    0x55
#define SEESAW_HW_ID_TINY806   0x84
#define SEESAW_HW_ID_TINY807   0x85
#define SEESAW_HW_ID_TINY816   0x86
#define SEESAW_HW_ID_TINY817   0x87
#define SEESAW_HW_ID_TINY1616  0x88
#define SEESAW_HW_ID_TINY1617  0x89

// Delay between the register write and the data read. The sensor needs time to
// take the capacitive measurement; Adafruit's library uses 3 ms (plus retries).
#define MOISTURE_READ_DELAY_US 3000
#define TEMP_READ_DELAY_US     1000
#define MOISTURE_READ_RETRIES  3      // only for "not ready yet" (0xFFFF) answers
#define STEMMA_PROBE_ATTEMPTS  10     // x 100 ms = up to 1 s for the sensor to power up

// Many callers (screen, web page, HomeKit, trend) ask for values; share one reading
#define MOISTURE_CACHE_MS      500
#define TEMP_CACHE_MS          5000
// After a bus error: reset the bus, then leave the sensor alone for a while
#define ERROR_BACKOFF_MS       5000
// Keep reporting the last good value through short outages
#define STALE_LIMIT_MS         60000

static int64_t now_ms()
{
	return esp_timer_get_time() / 1000;
}

StemmaSoil::StemmaSoil(i2c_master_bus_handle_t i2c_bus, uint8_t address)
    : m_bus(i2c_bus), m_address(address), m_lock(xSemaphoreCreateMutex())
{
}

bool StemmaSoil::attach(uint8_t address)
{
	if (m_i2c_handle != nullptr)
	{
		i2c_master_bus_rm_device(m_i2c_handle);
		m_i2c_handle = nullptr;
	}

	i2c_device_config_t i2c_dev_conf = {
	    .dev_addr_length = I2C_ADDR_BIT_LEN_7,
	    .device_address  = address,
	    .scl_speed_hz    = STEMMA_I2C_FREQ,
	    .scl_wait_us     = STEMMA_SCL_WAIT_US,
	    .flags           = {.disable_ack_check = 0},
	};

	esp_err_t ret = i2c_master_bus_add_device(m_bus, &i2c_dev_conf, &m_i2c_handle);
	if (ret != ESP_OK)
	{
		ESP_LOGE(TAG, "Failed to add i2c device %d", ret);
		m_i2c_handle = nullptr;
		return false;
	}
	m_address = address;
	return true;
}

// Find the sensor: try the configured address first, then the other jumper
// addresses. Retries for a while because the sensor may still be booting.
bool StemmaSoil::findAddress()
{
	uint8_t candidates[5] = {m_address, 0x36, 0x37, 0x38, 0x39};

	for (int attempt = 0; attempt < STEMMA_PROBE_ATTEMPTS; attempt++)
	{
		for (uint8_t addr : candidates)
		{
			if (i2c_master_probe(m_bus, addr, STEMMA_I2C_TIMEOUT_MS) == ESP_OK)
			{
				if (addr != m_address)
					ESP_LOGW(TAG, "Sensor found at 0x%x instead of 0x%x", addr, m_address);
				m_address = addr;
				return true;
			}
		}
		vTaskDelay(pdMS_TO_TICKS(100) > 0 ? pdMS_TO_TICKS(100) : 1);
	}

	// Not found: list whatever is on the bus to help diagnose wiring
	ESP_LOGE(TAG, "No soil sensor at 0x36-0x39. Devices on the I2C bus:");
	int found = 0;
	for (uint8_t addr = 0x08; addr < 0x78; addr++)
	{
		if (i2c_master_probe(m_bus, addr, STEMMA_I2C_TIMEOUT_MS) == ESP_OK)
		{
			ESP_LOGE(TAG, "  0x%02x", addr);
			found++;
		}
	}
	if (found == 0)
		ESP_LOGE(TAG, "  (none)");
	return false;
}

StemmaSoil::~StemmaSoil()
{
	if (m_i2c_handle != nullptr)
	{
		i2c_master_bus_rm_device(m_i2c_handle);
	}
	if (m_lock)
		vSemaphoreDelete(m_lock);
}

static void wait_us(uint32_t us)
{
	if (us < 1000)
	{
		esp_rom_delay_us(us);
		return;
	}
	// Sleep instead of spinning; round up and add a tick so we wait at least `us`
	TickType_t ticks = (us / 1000 + portTICK_PERIOD_MS - 1) / portTICK_PERIOD_MS + 1;
	vTaskDelay(ticks);
}

bool StemmaSoil::init()
{
	xSemaphoreTake(m_lock, portMAX_DELAY);
	bool ok = initLocked();
	xSemaphoreGive(m_lock);
	return ok;
}

bool StemmaSoil::initLocked()
{
	m_ready = false;
	if (!findAddress() || !attach(m_address))
		return false;

	uint8_t rst = 0xFF;
	if (!write(SEESAW_STATUS_BASE, SEESAW_STATUS_SWRST, &rst, 1))
	{
		ESP_LOGE(TAG, "Reset failed at 0x%x", m_address);
		return false;
	}

	// Wait for the sensor to come back after reset, polling the HW ID
	uint8_t hw_id = 0;
	for (int i = 0; i < 10; i++)
	{
		vTaskDelay(pdMS_TO_TICKS(10) > 0 ? pdMS_TO_TICKS(10) : 1);
		if (read(SEESAW_STATUS_BASE, SEESAW_STATUS_HW_ID, &hw_id, 1, 0))
		{
			if (hw_id == SEESAW_HW_ID_SAMD09 || (hw_id >= SEESAW_HW_ID_TINY806 && hw_id <= SEESAW_HW_ID_TINY1617))
				break;
		}
		hw_id = 0;
	}

	if (hw_id == 0)
	{
		ESP_LOGE(TAG, "Device at 0x%x is not a seesaw sensor (bad HW ID)", m_address);
		return false;
	}

	uint8_t ver[4] = {};
	if (read(SEESAW_STATUS_BASE, SEESAW_STATUS_VERSION, ver, sizeof(ver), 0))
	{
		// upper 16 bits = product code (4026 for the soil sensor)
		uint16_t product = (ver[0] << 8) | ver[1];
		ESP_LOGI(TAG, "seesaw at 0x%x: HW ID 0x%02x, product %u", m_address, hw_id, product);
	}

	m_ready = true;
	return true;
}

// Called after an I2C error: clear the bus and pause reads for a while
void StemmaSoil::busError(const char *what)
{
	if (m_fail_count++ == 0)
		ESP_LOGE(TAG, "%s failed; resetting bus, retrying in %d s", what, ERROR_BACKOFF_MS / 1000);
	i2c_master_bus_reset(m_bus);
	m_backoff_until_ms = now_ms() + ERROR_BACKOFF_MS;
}

void StemmaSoil::busOk()
{
	if (m_fail_count > 0)
		ESP_LOGI(TAG, "Sensor responding again after %d failed read(s)", m_fail_count);
	m_fail_count = 0;
}

int StemmaSoil::readMoisture()
{
	if (!m_ready)
		return -1;

	xSemaphoreTake(m_lock, portMAX_DELAY);

	int64_t now    = now_ms();
	bool    fresh  = m_moist_ms && now - m_moist_ms < MOISTURE_CACHE_MS;
	bool    usable = m_moist_ms && now - m_moist_ms < STALE_LIMIT_MS;
	int     result = usable ? m_moist : -1;

	if (!fresh && now >= m_backoff_until_ms)
	{
		uint8_t buf[2];
		for (int retry = 0; retry < MOISTURE_READ_RETRIES; retry++)
		{
			if (!read(SEESAW_TOUCH_BASE, SEESAW_TOUCH_CH_OFFSET, buf, sizeof(buf), MOISTURE_READ_DELAY_US + retry * 1000))
			{
				busError("Moisture read");
				break;
			}
			uint16_t val = (buf[0] << 8) | buf[1];
			// 0xFFFF means the measurement wasn't ready yet
			if (val != 0xFFFF)
			{
				busOk();
				m_moist    = val;
				m_moist_ms = now_ms();
				result     = val;
				break;
			}
		}
	}

	xSemaphoreGive(m_lock);
	return result;
}

bool StemmaSoil::readTemperature(float *temp_c)
{
	if (!m_ready || temp_c == nullptr)
		return false;

	xSemaphoreTake(m_lock, portMAX_DELAY);

	int64_t now   = now_ms();
	bool    fresh = m_temp_ms && now - m_temp_ms < TEMP_CACHE_MS;
	bool    ok    = m_temp_ms && now - m_temp_ms < STALE_LIMIT_MS;

	if (!fresh && now >= m_backoff_until_ms)
	{
		uint8_t buf[4];
		if (read(SEESAW_STATUS_BASE, SEESAW_STATUS_TEMP, buf, sizeof(buf), TEMP_READ_DELAY_US))
		{
			busOk();
			// 16.16 fixed point, big-endian (top bits reserved)
			int32_t raw = ((int32_t)(buf[0] & 0x3F) << 24) | ((int32_t)buf[1] << 16) | ((int32_t)buf[2] << 8) | buf[3];
			m_temp      = raw / 65536.0f;
			m_temp_ms   = now_ms();
			ok          = true;
		}
		else
		{
			busError("Temperature read");
		}
	}

	if (ok)
		*temp_c = m_temp;

	xSemaphoreGive(m_lock);
	return ok;
}

// seesaw reads are split: write the 2-byte register address, wait, then read.
// A repeated-start read doesn't give the sensor time to prepare the data.
// Caller must hold m_lock.
bool StemmaSoil::read(uint8_t base, uint8_t reg, uint8_t *buf, size_t len, uint32_t delay_us)
{
	uint8_t addr[2] = {base, reg};

	esp_err_t ret = i2c_master_transmit(m_i2c_handle, addr, sizeof(addr), STEMMA_I2C_TIMEOUT_MS);
	if (ret == ESP_OK)
	{
		if (delay_us)
			wait_us(delay_us);
		else
			esp_rom_delay_us(250);

		ret = i2c_master_receive(m_i2c_handle, buf, len, STEMMA_I2C_TIMEOUT_MS);
	}
	else
	{
		ESP_LOGD(TAG, "read addr write err %d", ret);
	}

	if (ret != ESP_OK)
	{
		ESP_LOGD(TAG, "read err %d", ret);
		return false;
	}
	return true;
}

// Caller must hold m_lock.
bool StemmaSoil::write(uint8_t base, uint8_t reg, const uint8_t *buf, size_t len)
{
	uint8_t data[2 + 4];
	if (len > sizeof(data) - 2)
		return false;

	data[0] = base;
	data[1] = reg;
	memcpy(&data[2], buf, len);

	esp_err_t ret = i2c_master_transmit(m_i2c_handle, data, 2 + len, STEMMA_I2C_TIMEOUT_MS);
	if (ret != ESP_OK)
	{
		ESP_LOGD(TAG, "write err %d", ret);
		return false;
	}
	return true;
}
