#pragma once
#include <stdint.h>
#include <esp_adc/adc_oneshot.h>

#define MOIST_FILTER_SIZE 16

static constexpr int ADC_FULL_SCALE = (1 << 12) - 1;  // 12-bit ADC max value

class MoistInput
{
public:
	MoistInput(adc_unit_t unit, adc_channel_t channel);
	~MoistInput();

	uint8_t getMoisture();
	int     getAvgRaw();           // read + update filter, return averaged raw ADC
	void    setCal(int raw_dry, int raw_wet);  // apply and persist calibration
	void    setThreshold(uint8_t val);         // apply and persist threshold
	uint8_t getThreshold() const { return m_threshold; }
	void    load();                // load calibration + threshold from NVS on startup

private:
	adc_unit_t                m_unit;
	adc_channel_t             m_channel;
	adc_oneshot_unit_handle_t m_adc_handle = nullptr;
	bool                      m_ready      = false;

	int  m_buf[MOIST_FILTER_SIZE] = {};
	int  m_buf_idx                = 0;
	int  m_buf_count              = 0;  // ramps up to MOIST_FILTER_SIZE on startup
	int  m_sum                    = 0;

	bool    m_cal_valid = false;
	int     m_raw_dry   = 0;
	int     m_raw_wet   = 4095;
	uint8_t m_threshold = 50;

	int  readRaw();
	void init();
};

