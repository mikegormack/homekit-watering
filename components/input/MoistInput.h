#pragma once
#include <stdint.h>
#include <memory>
#include <esp_adc/adc_oneshot.h>
#include <driver/i2c_master.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <freertos/task.h>

#include <StemmaSoil.h>

#define MOIST_FILTER_SIZE        16   // readings averaged (16 x 15 s = 4 minutes)
#define MOIST_SAMPLE_INTERVAL_MS 15000

static constexpr int ADC_FULL_SCALE = (1 << 12) - 1;  // 12-bit ADC max value

// Soil moisture input. A background task reads the sensor every 15 s; all the
// getters return the values from that task and never touch the sensor themselves.
class MoistInput
{
public:
	MoistInput(adc_unit_t unit, adc_channel_t channel);         // analog sensor on an ADC pin
	MoistInput(i2c_master_bus_handle_t i2c_bus, uint8_t address); // Adafruit STEMMA soil sensor (I2C)
	~MoistInput();

	uint8_t getMoisture();          // filtered moisture %, 0 if no reading yet
	int     getAvgRaw();            // filtered raw value, -1 if no reading yet
	int     getLatestRaw();         // most recent single raw reading (for calibration), -1 if none
	bool    readTemperature(float *temp_c);  // latest soil temp; STEMMA only, false if none
	void    setCal(int raw_dry, int raw_wet);  // apply and persist calibration
	void    setThreshold(uint8_t val);         // apply and persist threshold
	uint8_t getThreshold() const { return m_threshold; }
	int     getRawDry()   const { return m_raw_dry; }
	int     getRawWet()   const { return m_raw_wet; }
	bool    isCalValid()  const { return m_cal_valid; }
	void    load();                // load calibration + threshold from NVS on startup

private:
	adc_unit_t                  m_unit;
	adc_channel_t               m_channel;
	adc_oneshot_unit_handle_t   m_adc_handle = nullptr;
	bool                        m_ready      = false;
	std::unique_ptr<StemmaSoil> m_stemma;  // set when using the I2C sensor

	SemaphoreHandle_t m_lock = nullptr;  // guards the values below
	TaskHandle_t      m_task = nullptr;

	int   m_buf[MOIST_FILTER_SIZE] = {};
	int   m_buf_idx                = 0;
	int   m_buf_count              = 0;  // ramps up to MOIST_FILTER_SIZE on startup
	int   m_sum                    = 0;
	int   m_latest                 = -1;
	float m_temp                   = 0;
	bool  m_temp_valid             = false;

	bool    m_cal_valid = false;
	int     m_raw_dry   = 0;
	int     m_raw_wet   = 4095;
	uint8_t m_threshold = 50;

	int  readRaw();
	void sample();
	void startSampling();
	void init();
	static void sampleTask(void *arg);
};
