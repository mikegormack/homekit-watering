#include <OutputChannel.h>

#include <esp_log.h>

static const char *TAG = "OutputChannel";

OutputChannel::OutputChannel(const char* name):
	m_name(name)
{
	m_evt[0].hour = 8;
	m_evt[0].min = 0;
	m_evt[0].duration = 0;
	m_evt[1].hour = 18;
	m_evt[1].min = 0;
	m_evt[1].duration = 0;
}

OutputChannel::~OutputChannel()
{

}

void OutputChannel::save()
{
	nvs_handle_t handle;
	esp_err_t err = nvs_open("storage", NVS_READWRITE, &handle);
	if (err == ESP_OK)
	{
		err = nvs_set_blob(handle, m_name, m_evt, sizeof(m_evt));
		nvs_close(handle);
	}
	if (err == ESP_OK)
	{
		ESP_LOGI(TAG, "Save %s ok", m_name);
	}
	else
	{
		ESP_LOGE(TAG, "Save %s err %d", m_name, err);
	}
}

void OutputChannel::load()
{
	nvs_handle_t handle;
	esp_err_t err = nvs_open("storage", NVS_READONLY, &handle);
	if (err == ESP_OK)
	{
		size_t len = sizeof(m_evt);
		err = nvs_get_blob(handle, m_name, m_evt, &len);
		nvs_close(handle);
	}
	if (err == ESP_OK)
	{
		ESP_LOGI(TAG, "Load %s ok sz %d", m_name, sizeof(m_evt));
	}
	else
	{
		ESP_LOGE(TAG, "Load %s err %d", m_name, err);
	}
}

