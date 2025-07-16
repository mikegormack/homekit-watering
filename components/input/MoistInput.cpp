
#include <nvs_flash.h>
#include <nvs.h>

#include <esp_log.h>

#include <MoistInput.h>

static const char *TAG = "MoistInput";

MoistInput::MoistInput(/* args */)
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

uint8_t MoistInput::getMoisture()
{
	//if (adc_oneshot_read(m_adc_handle, )

	return 0;
}

void MoistInput::init()
{
	// ADC handle and config
	adc_oneshot_unit_init_cfg_t init_config =
	{
	    .unit_id = ADC_UNIT_1,
	    .clk_src =  (adc_oneshot_clk_src_t)0,
	    .ulp_mode = ADC_ULP_MODE_DISABLE
	};

	// Initialize ADC unit
	esp_err_t ret = adc_oneshot_new_unit(&init_config, &m_adc_handle);
	if (ret != ESP_OK)
	{
		ESP_LOGE(TAG, "ADC new unit err %d", ret);
	}

	// Configure channel
	adc_oneshot_chan_cfg_t config =
	{
	    .atten = ADC_ATTEN_DB_0,
	    .bitwidth = ADC_BITWIDTH_12
	};

	ret = adc_oneshot_config_channel(m_adc_handle, ADC_CHANNEL_0, &config);
	if (ret != ESP_OK)
	{
		ESP_LOGE(TAG, "ADC new unit err %d", ret);
	}
	else
	{
		ESP_LOGI(TAG, "ADC init ok");
	}
	adc_cali_handle_t handle;
	adc_cali_line_fitting_config_t cali_config =
	{
	    .unit_id = ADC_UNIT_1,
	    .atten = ADC_ATTEN_DB_0,
	    .bitwidth = ADC_BITWIDTH_DEFAULT,
	    .default_vref = 0
	};
	ret = adc_cali_create_scheme_line_fitting(&cali_config, &handle);
}
