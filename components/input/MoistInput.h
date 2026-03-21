#pragma once
#include <stdint.h>
#include <esp_adc/adc_oneshot.h>

#define MOIST_FILTER_SIZE 16

class MoistInput
{
public:
	MoistInput(adc_unit_t unit, adc_channel_t channel);
	~MoistInput();

	uint8_t getMoisture();
private:
	adc_unit_t                m_unit;
	adc_channel_t             m_channel;
	adc_oneshot_unit_handle_t m_adc_handle = nullptr;
	bool                      m_ready      = false;

	int  m_buf[MOIST_FILTER_SIZE] = {};
	int  m_buf_idx                = 0;
	int  m_buf_count              = 0;  // ramps up to MOIST_FILTER_SIZE on startup
	int  m_sum                    = 0;

	int  readRaw();
	void init();
};

