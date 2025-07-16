#pragma once
#include <stdint.h>
#include <esp_adc/adc_oneshot.h>

class MoistInput
{
public:
	MoistInput(/* args */);
	~MoistInput();

	uint8_t getMoisture();
private:
	adc_oneshot_unit_handle_t m_adc_handle = nullptr;
	void init();
};

