/*
 * This supports 3 ways of connecting to the Wi-Fi Network
 * 1. Hard coded credentials
 * 2. Unified Provisioning
 * 3. Apple Wi-Fi Accessory Configuration (WAC. Available only on MFi variant of the SDK)
 *
 * Unified Provisioning has 2 options
 * 1. BLE Provisioning
 * 2. SoftAP Provisioning
 *
 * Unified and WAC Provisioning can co-exist.
 * If the SoftAP Unified Provisioning is being used, the same SoftAP interface
 * will be used for WAC. However, if BLE Unified Provisioning is used, the HAP
 * Helper functions for softAP start/stop will be used
 */

#include <memory>

#include <string.h>

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/event_groups.h>
#include <esp_wifi.h>
#include <esp_event.h>
#include <esp_log.h>
#include <esp_idf_version.h>

#if ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(4, 1, 0)
// Features supported in 4.1+
#define ESP_NETIF_SUPPORTED
#endif

#ifdef ESP_NETIF_SUPPORTED
#include <esp_netif.h>
#else
#include <tcpip_adapter.h>
#endif

#include <wifi_provisioning/manager.h>

#ifdef CONFIG_APP_WIFI_USE_UNIFIED_PROVISIONING

#include <wifi_provisioning/scheme_ble.h>
#include <qrcodegen.h>
#endif /* CONFIG_APP_WIFI_USE_UNIFIED_PROVISIONING */

#include <nvs.h>
#include <nvs_flash.h>
#include <app_wifi_handler.h>

static const char*        TAG = "app_wifi_handler";
static const int          WIFI_CONNECTED_EVENT = BIT0;
static EventGroupHandle_t wifi_event_group;
static bool               s_prov_mgr_init = false;
static bool               s_prov_failed = false;
static esp_netif_t*       s_wifi_netif = nullptr;
static wifi_config_t      s_saved_wifi_cfg = {};
static bool               s_has_saved_cfg = false;

#ifdef CONFIG_APP_WIFI_USE_UNIFIED_PROVISIONING
#define PROV_QR_VERSION "v1"

#define MAX_QRCODE_VERSION 4

#define PROV_TRANSPORT_SOFTAP "softap"
#define PROV_TRANSPORT_BLE    "ble"
#define QRCODE_BASE_URL       "https://espressif.github.io/esp-jumpstart/qrcode.html"

#define CREDENTIALS_NAMESPACE "rmaker_creds"
#define RANDOM_NVS_KEY        "random"

static std::unique_ptr<uint8_t[]> gen_qr_code(const char* name, const char* pop, const char* transport)
{
	if (!name || !pop || !transport)
	{
		ESP_LOGE(TAG, "Cannot generate QR code payload. Data missing.");
		return nullptr;
	}

	char payload[150];
	snprintf(payload, sizeof(payload),
	         "{\"ver\":\"%s\",\"name\":\"%s\""
	         ",\"pop\":\"%s\",\"transport\":\"%s\"}",
	         PROV_QR_VERSION, name, pop, transport);

	ESP_LOGI(TAG, "Generated QR code payload: %s", payload);

	enum qrcodegen_Ecc         errCorLvl = qrcodegen_Ecc_LOW;
	const int                  buf_len = qrcodegen_BUFFER_LEN_FOR_VERSION(MAX_QRCODE_VERSION);
	std::unique_ptr<uint8_t[]> qrcode(new (std::nothrow) uint8_t[buf_len]);
	std::unique_ptr<uint8_t[]> temp(new (std::nothrow) uint8_t[buf_len]);
	if (!qrcode || !temp)
	{
		ESP_LOGE(TAG, "Failed to allocate QR buffers");
		return nullptr;
	}
	memset(qrcode.get(), 0, buf_len);
	memset(temp.get(), 0, buf_len);

	// Make and print the QR Code symbol
	bool ok = qrcodegen_encodeText(payload, temp.get(), qrcode.get(), errCorLvl, qrcodegen_VERSION_MIN,
	                               MAX_QRCODE_VERSION, qrcodegen_Mask_AUTO, true);
	if (!ok)
	{
		ESP_LOGE(TAG, "qrcodegen_encodeText failed");
		return nullptr;
	}

	// Generate QR code
	return qrcode;
}

static void get_device_service_name(char* service_name, size_t max)
{
	uint8_t     eth_mac[6];
	const char* ssid_prefix = "PROV_";
	esp_wifi_get_mac(WIFI_IF_STA, eth_mac);
	snprintf(service_name, max, "%s%02X%02X%02X", ssid_prefix, eth_mac[3], eth_mac[4], eth_mac[5]);
}

static esp_err_t get_device_pop(char* pop, size_t max)
{
	if (!pop || !max)
	{
		return ESP_ERR_INVALID_ARG;
	}

	uint8_t   eth_mac[6];
	esp_err_t err = esp_wifi_get_mac(WIFI_IF_STA, eth_mac);
	if (err == ESP_OK)
	{
		snprintf(pop, max, "%02x%02x%02x%02x", eth_mac[2], eth_mac[3], eth_mac[4], eth_mac[5]);
		return ESP_OK;
	}
	else
	{
		return err;
	}
}
#endif /* #ifdef CONFIG_APP_WIFI_USE_UNIFIED_PROVISIONING */

/* Event handler for catching system events */
static void event_handler(void* arg, esp_event_base_t event_base, int32_t event_id, void* event_data)
{
	if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START)
	{
		esp_err_t err = esp_wifi_connect();
		ESP_LOGI(TAG, "STA start - esp_wifi_connect: %s", esp_err_to_name(err));
	}
	else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_CONNECTED)
	{
		esp_netif_create_ip6_linklocal((esp_netif_t*)arg);
	}
	else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP)
	{
		ip_event_got_ip_t* event = (ip_event_got_ip_t*)event_data;
		ESP_LOGI(TAG, "Connected with IP Address:" IPSTR, IP2STR(&event->ip_info.ip));
		// Signal main application to continue execution
		xEventGroupSetBits(wifi_event_group, WIFI_CONNECTED_EVENT);
	}
	else if (event_base == IP_EVENT && event_id == IP_EVENT_GOT_IP6)
	{
		ip_event_got_ip6_t* event = (ip_event_got_ip6_t*)event_data;
		ESP_LOGI(TAG, "Connected with IPv6 Address:" IPV6STR, IPV62STR(event->ip6_info.ip));
	}
	else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED)
	{
		ESP_LOGI(TAG, "Disconnected. Connecting to the AP again...");
		esp_wifi_connect();
	}
#ifdef CONFIG_APP_WIFI_USE_UNIFIED_PROVISIONING
	else if (event_base == WIFI_PROV_EVENT)
	{
		switch (event_id)
		{
			case WIFI_PROV_START:
				ESP_LOGI(TAG, "Provisioning started");
				break;
			case WIFI_PROV_CRED_RECV:
			{
				wifi_sta_config_t* wifi_sta_cfg = (wifi_sta_config_t*)event_data;
				ESP_LOGI(TAG,
				         "Received Wi-Fi credentials"
				         "\n\tSSID     : %s\n\tPassword : %s",
				         (const char*)wifi_sta_cfg->ssid, (const char*)wifi_sta_cfg->password);
				break;
			}
			case WIFI_PROV_CRED_FAIL:
			{
				wifi_prov_sta_fail_reason_t* reason = (wifi_prov_sta_fail_reason_t*)event_data;
				ESP_LOGE(TAG,
				         "Provisioning failed!\n\tReason : %s",
				         (*reason == WIFI_PROV_STA_AUTH_ERROR) ? "Wi-Fi station authentication failed"
				                                               : "Wi-Fi access-point not found");
				s_prov_failed = true;
				break;
			}
			case WIFI_PROV_CRED_SUCCESS:
				ESP_LOGI(TAG, "Provisioning successful");
				break;
			case WIFI_PROV_END:
			{
				ESP_LOGI(TAG, "Provisioning end");
				s_prov_mgr_init = false;
				wifi_prov_mgr_deinit();
				esp_err_t err;
				err = esp_wifi_stop();
				ESP_LOGI(TAG, "esp_wifi_stop: %s", esp_err_to_name(err));
				err = esp_wifi_set_mode(WIFI_MODE_STA);
				ESP_LOGI(TAG, "esp_wifi_set_mode STA: %s", esp_err_to_name(err));
				if (s_has_saved_cfg)
				{
					err = esp_wifi_set_config(WIFI_IF_STA, &s_saved_wifi_cfg);
					ESP_LOGI(TAG, "esp_wifi_set_config (restore): %s", esp_err_to_name(err));
				}
				err = esp_wifi_start();
				ESP_LOGI(TAG, "esp_wifi_start: %s", esp_err_to_name(err));
				break;
			}
			default:
				break;
		}
	}
#endif /* CONFIG_APP_WIFI_USE_UNIFIED_PROVISIONING */
}

static esp_err_t prov_mgr_reinit(void)
{
	wifi_prov_mgr_config_t config = {.scheme               = wifi_prov_scheme_ble,
	                                 .scheme_event_handler = WIFI_PROV_EVENT_HANDLER_NONE,
	                                 .app_event_handler    = {.event_cb = NULL, .user_data = NULL},
	                                 .wifi_prov_conn_cfg   = {0}};
	esp_err_t err = wifi_prov_mgr_init(config);
	if (err != ESP_OK)
	{
		ESP_LOGE(TAG, "Failed to initialize provisioning manager: %s", esp_err_to_name(err));
		return err;
	}
	s_prov_mgr_init = true;
	return ESP_OK;
}

bool app_wifi_handler_init(void)
{
	esp_err_t err;
	err = esp_netif_init();
	if (err != ESP_OK)
	{
		ESP_LOGE(TAG, "Failed to initialize netif: %s", esp_err_to_name(err));
		return false;
	}

	err = esp_event_loop_create_default();
	if (err != ESP_OK)
	{
		ESP_LOGE(TAG, "Failed to create event loop: %s", esp_err_to_name(err));
		return false;
	}
	wifi_event_group = xEventGroupCreate();

	s_wifi_netif = esp_netif_create_default_wifi_sta();

	// Register our event handler for Wi-Fi, IP and Provisioning related events
	err = esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &event_handler, s_wifi_netif);
	if (err != ESP_OK)
	{
		ESP_LOGE(TAG, "Failed to register Wi-Fi event handler: %s", esp_err_to_name(err));
		return false;
	}

	err = esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &event_handler, NULL);
	if (err != ESP_OK)
	{
		ESP_LOGE(TAG, "Failed to register IP_EVENT_STA_GOT_IP event handler: %s", esp_err_to_name(err));
		return false;
	}

	err = esp_event_handler_register(IP_EVENT, IP_EVENT_GOT_IP6, &event_handler, NULL);
	if (err != ESP_OK)
	{
		ESP_LOGE(TAG, "Failed to register IP_EVENT_GOT_IP6 event handler: %s", esp_err_to_name(err));
		return false;
	}

	wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
	err = esp_wifi_init(&cfg);
	if (err != ESP_OK)
	{
		ESP_LOGE(TAG, "Failed to initialize Wi-Fi: %s", esp_err_to_name(err));
		return false;
	}
	ESP_LOGI(TAG, "Wi-Fi initialized successfully");

#ifdef CONFIG_APP_WIFI_USE_UNIFIED_PROVISIONING
	if (prov_mgr_reinit() != ESP_OK)
		return false;

	err = esp_event_handler_register(WIFI_PROV_EVENT, ESP_EVENT_ANY_ID, &event_handler, NULL);
	if (err != ESP_OK)
	{
		ESP_LOGE(TAG, "Failed to register WIFI_PROV_EVENT handler: %s", esp_err_to_name(err));
		return false;
	}

	// If already provisioned, skip the manager and connect directly
	bool provisioned = false;
	wifi_prov_mgr_is_provisioned(&provisioned);
	if (provisioned)
	{
		ESP_LOGI(TAG, "Already provisioned — starting Wi-Fi STA");
		s_prov_mgr_init = false;
		wifi_prov_mgr_deinit();
		esp_wifi_set_mode(WIFI_MODE_STA);
		esp_wifi_start(); // WIFI_EVENT_STA_START handler calls esp_wifi_connect()
	}
#endif /* CONFIG_APP_WIFI_USE_UNIFIED_PROVISIONING */

	return true;
}

#ifdef CONFIG_APP_WIFI_USE_HARDCODED
#define APP_WIFI_SSID CONFIG_APP_WIFI_SSID
#define APP_WIFI_PASS CONFIG_APP_WIFI_PASSWORD

esp_err_t app_wifi_handler_start(TickType_t ticks_to_wait)
{
	wifi_config_t wifi_config = {
	    .sta =
	        {
	            .ssid = APP_WIFI_SSID,
	            .password = APP_WIFI_PASS,
	            /* Setting a password implies station will connect to all security modes including WEP/WPA.
	             * However these modes are deprecated and not advisable to be used. Incase your Access point
	             * doesn't support WPA2, these mode can be enabled by commenting below line */
	            .threshold.authmode = WIFI_AUTH_WPA2_PSK,

	            .pmf_cfg = {.capable = true, .required = false},
	        },
	};
	ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
	ESP_ERROR_CHECK(esp_wifi_set_config(ESP_IF_WIFI_STA, &wifi_config));
	ESP_ERROR_CHECK(esp_wifi_start());
	/* Wait for Wi-Fi connection */
	xEventGroupWaitBits(wifi_event_group, WIFI_CONNECTED_EVENT, false, true, ticks_to_wait);
	return ESP_OK;
	return ESP_OK;
}
#endif

#ifdef CONFIG_APP_WIFI_USE_PROVISIONING
static void wifi_init_sta()
{
	ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
	ESP_ERROR_CHECK(esp_wifi_start());
}
#endif

bool wifi_handler_is_prov_active(void)
{
	if (!s_prov_mgr_init)
		return false;
	return (wifi_prov_mgr_is_sm_idle() == false);
}

std::unique_ptr<uint8_t[]> wifi_handler_start_provisioning(void)
{
	if (wifi_handler_is_prov_active())
	{
		ESP_LOGW(TAG, "Provisioning is already active");
		return nullptr;
	}

	// Re-initialize manager if it was previously deinitialized after a session
	if (!s_prov_mgr_init)
	{
		if (prov_mgr_reinit() != ESP_OK)
			return nullptr;
	}

	// Clear any stale state so the screen detects fresh events only
	xEventGroupClearBits(wifi_event_group, WIFI_CONNECTED_EVENT);
	s_prov_failed = false;

	// Save current WiFi config — provisioning wipes the RAM config
	s_has_saved_cfg = false;
	wifi_config_t cfg = {};
	if (esp_wifi_get_config(WIFI_IF_STA, &cfg) == ESP_OK && strlen((char*)cfg.sta.ssid) > 0)
	{
		s_saved_wifi_cfg = cfg;
		s_has_saved_cfg  = true;
		ESP_LOGI(TAG, "Saved WiFi config for SSID: %s", cfg.sta.ssid);
	}

	char                 service_name[12];
	char                 pop[9];
	wifi_prov_security_t security = WIFI_PROV_SECURITY_1;
	const char*          service_key = NULL;

	get_device_service_name(service_name, sizeof(service_name));

	esp_err_t err = get_device_pop(pop, sizeof(pop));
	if (err != ESP_OK)
	{
		ESP_LOGE(TAG, "Error: %d. Failed to get PoP from NVS, Please perform Claiming.", err);
		return nullptr;
	}

	uint8_t custom_service_uuid[] = {
	    // This is a random uuid. This can be modified if you want to change the BLE uuid.
	    0xb4, 0xdf, 0x5a, 0x1c, 0x3f, 0x6b, 0xf4, 0xbf, 0xea, 0x4a, 0x82, 0x03, 0x04, 0x90, 0x1a, 0x02,
	};
	err = wifi_prov_scheme_ble_set_service_uuid(custom_service_uuid);
	if (err != ESP_OK)
	{
		ESP_LOGE(TAG, "wifi_prov_scheme_ble_set_service_uuid failed %d", err);
		return nullptr;
	}

	// Start the provisioning process
	err = wifi_prov_mgr_start_provisioning(security, pop, service_name, service_key);
	if (err != ESP_OK)
	{
		ESP_LOGE(TAG, "Failed to start provisioning: %s", esp_err_to_name(err));
		return nullptr;
	}

	return gen_qr_code(service_name, pop, PROV_TRANSPORT_BLE);
}

void wifi_handler_stop_provisioning(void)
{
	wifi_prov_mgr_stop_provisioning();
}

bool wifi_handler_is_connected(void)
{
	if (wifi_event_group == NULL)
		return false;
	return (xEventGroupGetBits(wifi_event_group) & WIFI_CONNECTED_EVENT) != 0;
}

bool wifi_handler_prov_failed(void)
{
	return s_prov_failed;
}

void wifi_handler_get_info(char* ssid, size_t ssid_len, char* ip, size_t ip_len, bool* connected)
{
	*connected = false;
	if (ssid && ssid_len) ssid[0] = '\0';
	if (ip && ip_len)     ip[0]   = '\0';

	wifi_ap_record_t ap;
	if (esp_wifi_sta_get_ap_info(&ap) == ESP_OK)
	{
		*connected = true;
		if (ssid && ssid_len)
			snprintf(ssid, ssid_len, "%s", (char*)ap.ssid);
	}

	if (s_wifi_netif && ip && ip_len)
	{
		esp_netif_ip_info_t ip_info;
		if (esp_netif_get_ip_info(s_wifi_netif, &ip_info) == ESP_OK)
			snprintf(ip, ip_len, IPSTR, IP2STR(&ip_info.ip));
	}
}
