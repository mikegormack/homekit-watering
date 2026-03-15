/*
   This example code is in the Public Domain (or CC0 licensed, at your option.)

   Unless required by applicable law or agreed to in writing, this
   software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
   CONDITIONS OF ANY KIND, either express or implied.
*/
#pragma once
#include <esp_err.h>
#include <memory>

#ifdef __cplusplus
extern "C"
{
#endif

	bool      app_wifi_handler_init(void);
	esp_err_t app_wifi_handler_start(TickType_t ticks_to_wait);

	std::unique_ptr<uint8_t[]> wifi_handler_start_provisioning(void);
	void                       wifi_handler_stop_provisioning(void);
	bool                       wifi_handler_is_connected(void);
	bool                       wifi_handler_prov_failed(void);
	void                       wifi_handler_get_info(char* ssid, size_t ssid_len, char* ip, size_t ip_len, bool* connected);
#ifdef __cplusplus
}
#endif
