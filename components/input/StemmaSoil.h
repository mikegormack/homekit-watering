#pragma once
#include <stdint.h>
#include <driver/i2c_master.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

// Adafruit STEMMA capacitive soil moisture sensor (product 4026).
// The board runs Adafruit "seesaw" firmware on an ATSAMD10 and talks I2C.
// Default address 0x36; the AD0/AD1 jumpers select 0x37-0x39.

#define STEMMA_SOIL_BASE_ADDRESS 0x36

// Typical raw readings from Adafruit's docs: ~200 in air, ~2000 in wet soil.
static constexpr int STEMMA_SOIL_RAW_DRY = 200;
static constexpr int STEMMA_SOIL_RAW_WET = 2000;

class StemmaSoil
{
public:
	StemmaSoil(i2c_master_bus_handle_t i2c_bus, uint8_t address = STEMMA_SOIL_BASE_ADDRESS);
	~StemmaSoil();

	/*********************************************************************
	 * @brief soft-reset the sensor and check the hardware ID
	 *
	 * @return true = sensor found and responding
	 */
	bool init();

	bool    isReady() const { return m_ready; }
	uint8_t address() const { return m_address; }  // may differ from the requested one if auto-detected

	/*********************************************************************
	 * @brief read the capacitive moisture value
	 *
	 * Readings are cached for 500 ms. After an I2C error the bus is reset and the
	 * sensor is left alone for 5 s; meanwhile the last good value is returned
	 * (for up to 60 s).
	 *
	 * @return raw reading (roughly 200 dry .. 2000 wet), or -1 on error
	 */
	int readMoisture();

	/*********************************************************************
	 * @brief read the on-chip temperature sensor (about +/- 2 C)
	 *
	 * @param temp_c receives the temperature in degrees C
	 * @return true = success
	 */
	bool readTemperature(float *temp_c);

private:
	i2c_master_bus_handle_t m_bus;
	i2c_master_dev_handle_t m_i2c_handle = nullptr;
	uint8_t                 m_address;
	bool                    m_ready = false;
	SemaphoreHandle_t       m_lock;  // a read is write-wait-read; keep tasks from interleaving

	// Last good readings, shared by all callers (guarded by m_lock)
	int     m_moist            = -1;
	int64_t m_moist_ms         = 0;
	float   m_temp             = 0;
	int64_t m_temp_ms          = 0;
	int64_t m_backoff_until_ms = 0;
	int     m_fail_count       = 0;

	bool initLocked();
	void busError(const char *what);
	void busOk();
	bool findAddress();
	bool attach(uint8_t address);
	bool read(uint8_t base, uint8_t reg, uint8_t *buf, size_t len, uint32_t delay_us);
	bool write(uint8_t base, uint8_t reg, const uint8_t *buf, size_t len);
};
