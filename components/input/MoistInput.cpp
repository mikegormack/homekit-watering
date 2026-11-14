
#include <esp_log.h>
#include <nvs_flash.h>
#include <nvs.h>

#include <MoistInput.h>

static const char *TAG = "MoistInput";

MoistInput::MoistInput(adc_unit_t unit, adc_channel_t channel) : m_unit(unit), m_channel(channel)
{
	init();
	startSampling();
}

MoistInput::MoistInput(i2c_master_bus_handle_t i2c_bus, uint8_t address)
    : m_unit(ADC_UNIT_1), m_channel(ADC_CHANNEL_0), m_stemma(std::make_unique<StemmaSoil>(i2c_bus, address))
{
	m_ready = m_stemma->init();
	if (m_ready)
		ESP_LOGI(TAG, "STEMMA soil sensor at 0x%x init ok", m_stemma->address());
	else
		ESP_LOGE(TAG, "STEMMA soil sensor at 0x%x not found", address);
	startSampling();
}

MoistInput::~MoistInput()
{
	if (m_task != nullptr)
		vTaskDelete(m_task);
	if (m_lock != nullptr)
		vSemaphoreDelete(m_lock);
	if (m_adc_handle != nullptr)
	{
		adc_oneshot_del_unit(m_adc_handle);
	}
}

int MoistInput::readRaw()
{
	if (m_stemma)
		return m_stemma->readMoisture();

	int raw = 0;
	esp_err_t ret = adc_oneshot_read(m_adc_handle, m_channel, &raw);
	if (ret != ESP_OK)
	{
		ESP_LOGE(TAG, "ADC read err %d", ret);
		return -1;
	}
	return raw;
}

void MoistInput::startSampling()
{
	m_lock = xSemaphoreCreateMutex();
	if (!m_ready)
		return;
	if (xTaskCreate(sampleTask, "moist", 3072, this, 1, &m_task) != pdPASS)
	{
		ESP_LOGE(TAG, "Failed to start sampling task");
		m_task = nullptr;
	}
}

void MoistInput::sampleTask(void *arg)
{
	MoistInput *self = static_cast<MoistInput *>(arg);
	for (;;)
	{
		self->sample();
		vTaskDelay(pdMS_TO_TICKS(MOIST_SAMPLE_INTERVAL_MS));
	}
}

// The only place the sensor is read
void MoistInput::sample()
{
	int   raw        = readRaw();
	float temp       = 0;
	bool  temp_valid = m_stemma && m_stemma->readTemperature(&temp);

	xSemaphoreTake(m_lock, portMAX_DELAY);
	if (raw >= 0)
	{
		m_latest = raw;
		m_sum -= m_buf[m_buf_idx];
		m_buf[m_buf_idx] = raw;
		m_sum += raw;
		m_buf_idx = (m_buf_idx + 1) % MOIST_FILTER_SIZE;
		if (m_buf_count < MOIST_FILTER_SIZE)
			m_buf_count++;
	}
	m_temp       = temp;
	m_temp_valid = temp_valid;
	int avg      = m_buf_count ? m_sum / m_buf_count : -1;
	xSemaphoreGive(m_lock);

	if (temp_valid)
		ESP_LOGI(TAG, "Sample raw=%d avg=%d temp=%.1fC", raw, avg, temp);
	else
		ESP_LOGI(TAG, "Sample raw=%d avg=%d", raw, avg);
}

int MoistInput::getAvgRaw()
{
	if (!m_ready || !m_lock)
		return -1;
	xSemaphoreTake(m_lock, portMAX_DELAY);
	int avg = m_buf_count ? m_sum / m_buf_count : -1;
	xSemaphoreGive(m_lock);
	return avg;
}

int MoistInput::getLatestRaw()
{
	if (!m_ready || !m_lock)
		return -1;
	xSemaphoreTake(m_lock, portMAX_DELAY);
	int v = m_latest;
	xSemaphoreGive(m_lock);
	return v;
}

uint8_t MoistInput::getMoisture()
{
	int avg = getAvgRaw();
	if (avg < 0)
		return 0;

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

	if (m_stemma)
	{
		// Uncalibrated: map the sensor's typical dry..wet range
		int pct = (avg - STEMMA_SOIL_RAW_DRY) * 100 / (STEMMA_SOIL_RAW_WET - STEMMA_SOIL_RAW_DRY);
		if (pct < 0)   pct = 0;
		if (pct > 100) pct = 100;
		return (uint8_t)pct;
	}

	// Uncalibrated: map full 12-bit range
	return (uint8_t)((avg * 100) / ADC_FULL_SCALE);
}

bool MoistInput::readTemperature(float *temp_c)
{
	if (!m_ready || !m_stemma || !m_lock || temp_c == nullptr)
		return false;
	xSemaphoreTake(m_lock, portMAX_DELAY);
	bool ok = m_temp_valid;
	if (ok)
		*temp_c = m_temp;
	xSemaphoreGive(m_lock);
	return ok;
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
