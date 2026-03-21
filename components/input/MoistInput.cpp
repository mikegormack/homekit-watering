
#include <esp_log.h>

#include <MoistInput.h>

static const char *TAG = "MoistInput";

MoistInput::MoistInput(adc_unit_t unit, adc_channel_t channel) : m_unit(unit), m_channel(channel)
{
	init();
}

MoistInput::~MoistInput()
{
	if (m_adc_handle != nullptr)
	{
		adc_oneshot_del_unit(m_adc_handle);
	}
}

int MoistInput::readRaw()
{
	int raw = 0;
	esp_err_t ret = adc_oneshot_read(m_adc_handle, m_channel, &raw);
	if (ret != ESP_OK)
	{
		ESP_LOGE(TAG, "ADC read err %d", ret);
		return -1;
	}
	return raw;
}

uint8_t MoistInput::getMoisture()
{
	if (!m_ready)
		return 0;

	int raw = readRaw();
	if (raw < 0)
		return 0;

	// Update circular buffer
	m_sum -= m_buf[m_buf_idx];
	m_buf[m_buf_idx] = raw;
	m_sum += raw;
	m_buf_idx = (m_buf_idx + 1) % MOIST_FILTER_SIZE;
	if (m_buf_count < MOIST_FILTER_SIZE)
		m_buf_count++;

	int avg = m_sum / m_buf_count;
	ESP_LOGI(TAG, "ADC raw %d avg %d", raw, avg);

	// Map 12-bit ADC value (0-4095) to 0-100%
	return (uint8_t)((avg * 100) / 4095);
}

void MoistInput::init()
{
	adc_oneshot_unit_init_cfg_t init_config = {
	    .unit_id  = m_unit,
	    .clk_src  = (adc_oneshot_clk_src_t)0,
	    .ulp_mode = ADC_ULP_MODE_DISABLE,
	};

	esp_err_t ret = adc_oneshot_new_unit(&init_config, &m_adc_handle);
	if (ret != ESP_OK)
	{
		ESP_LOGE(TAG, "ADC unit init err %d", ret);
		return;
	}

	adc_oneshot_chan_cfg_t config = {
	    .atten    = ADC_ATTEN_DB_12,   // full 0–3.3 V range
	    .bitwidth = ADC_BITWIDTH_12,
	};

	ret = adc_oneshot_config_channel(m_adc_handle, m_channel, &config);
	if (ret != ESP_OK)
	{
		ESP_LOGE(TAG, "ADC channel config err %d", ret);
		return;
	}

	ESP_LOGI(TAG, "ADC unit %d ch %d init ok", (int)m_unit, (int)m_channel);
	m_ready = true;
}
