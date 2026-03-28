
#include <esp_log.h>
#include <nvs_flash.h>
#include <nvs.h>

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

int MoistInput::getAvgRaw()
{
	if (!m_ready)
		return -1;

	int raw = readRaw();
	if (raw < 0)
		return -1;

	m_sum -= m_buf[m_buf_idx];
	m_buf[m_buf_idx] = raw;
	m_sum += raw;
	m_buf_idx = (m_buf_idx + 1) % MOIST_FILTER_SIZE;
	if (m_buf_count < MOIST_FILTER_SIZE)
		m_buf_count++;

	return m_sum / m_buf_count;
}

uint8_t MoistInput::getMoisture()
{
	if (!m_ready)
		return 0;

	int avg = getAvgRaw();
	if (avg < 0)
		return 0;

	ESP_LOGI(TAG, "ADC avg %d", avg);

	if (m_cal_valid)
	{
		int range = m_raw_wet - m_raw_dry;
		if (range == 0)
			return 0;
		int pct = (avg - m_raw_dry) * 100 / range;
		if (pct < 0)   pct = 0;
		if (pct > 100) pct = 100;
		return (uint8_t)pct;
	}

	// Uncalibrated: map full 12-bit range
	return (uint8_t)((avg * 100) / ADC_FULL_SCALE);
}

void MoistInput::setCal(int raw_dry, int raw_wet)
{
	m_raw_dry   = raw_dry;
	m_raw_wet   = raw_wet;
	m_cal_valid = (raw_wet != raw_dry);

	nvs_handle_t h;
	if (nvs_open("storage", NVS_READWRITE, &h) == ESP_OK)
	{
		nvs_set_i32(h, "moist_dry", raw_dry);
		nvs_set_i32(h, "moist_wet", raw_wet);
		nvs_commit(h);
		nvs_close(h);
		ESP_LOGI(TAG, "Cal saved: dry=%d wet=%d", raw_dry, raw_wet);
	}
}

void MoistInput::setThreshold(uint8_t val)
{
	m_threshold = val;

	nvs_handle_t h;
	if (nvs_open("storage", NVS_READWRITE, &h) == ESP_OK)
	{
		nvs_set_u8(h, "moist_thr", val);
		nvs_commit(h);
		nvs_close(h);
		ESP_LOGI(TAG, "Threshold saved: %d", val);
	}
}

void MoistInput::load()
{
	nvs_handle_t h;
	if (nvs_open("storage", NVS_READONLY, &h) != ESP_OK)
		return;

	int32_t dry = 0, wet = 4095;
	if ((nvs_get_i32(h, "moist_dry", &dry) == ESP_OK) &&
	    (nvs_get_i32(h, "moist_wet", &wet) == ESP_OK) && dry != wet)
	{
		m_raw_dry   = (int)dry;
		m_raw_wet   = (int)wet;
		m_cal_valid = true;
		ESP_LOGI(TAG, "Cal loaded: dry=%d wet=%d", m_raw_dry, m_raw_wet);
	}

	uint8_t thr = 50;
	if (nvs_get_u8(h, "moist_thr", &thr) == ESP_OK)
	{
		m_threshold = thr;
		ESP_LOGI(TAG, "Threshold loaded: %d", thr);
	}

	nvs_close(h);
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
