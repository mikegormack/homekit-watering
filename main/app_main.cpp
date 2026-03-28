/*
 * ESPRESSIF MIT License
 *
 * Copyright (c) 2018 <ESPRESSIF SYSTEMS (SHANGHAI) PTE LTD>
 *
 * Permission is hereby granted for use on ESPRESSIF SYSTEMS products only, in which case,
 * it is free of charge, to any person obtaining a copy of this software and associated
 * documentation files (the "Software"), to deal in the Software without restriction, including
 * without limitation the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the Software is furnished
 * to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all copies or
 * substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS
 * FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR
 * COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
 * IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 *
 */

/* HomeKit Fan Example
 */

#include <stdio.h>
#include <string.h>
#include <ctime>
#include <memory>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <esp_timer.h>
#include <esp_event.h>
#include <esp_log.h>
#include <nvs_flash.h>
#include <nvs.h>

#include <driver/i2c.h>

#include <hap.h>
#include <hap_apple_servs.h>
#include <hap_apple_chars.h>

#include <hap_fw_upgrade.h>
#include <iot_button.h>

#include <esp_wifi.h>
#include <app_hap_setup_payload.h>
#include <app_wifi_handler.h>

extern const char* timezone_posix(int idx); // defined in TimezoneScreen.cpp

#include "ValveChannel.h"
#include "Scheduler.h"
#include "OtaServer.h"
#include "ControlServer.h"
#include "WebBase.h"
// WebBase owns the httpd instance; OtaServer and ControlServer register onto it
#include <MoistInput.h>
#include <UI.h>

#include <SSD1306I2C.h>
#include <MCP23017.h>

#include <iocfg.h>

/*  Required for server verification during OTA, PEM format as string  */
char server_cert[] = {};

static std::unique_ptr<ValveChannel> s_valves[2];
static Scheduler                     s_sched;
static WebBase                       s_web_base;
static OtaServer                     s_ota_server;
static ControlServer                 s_ctrl_server;

static std::unique_ptr<SSD1306I2C> disp_p;
static std::unique_ptr<UI>         ui_p;
static std::unique_ptr<MoistInput> moisture_p;
static std::shared_ptr<MCP23017>   ioexp_p;

static MenuCtx menu_ctx;
static int32_t s_tz_idx = 0;

static const char* TAG = "HAP Sprinkler";

#define SPRINKLER_TASK_PRIORITY  1
#define SPRINKLER_TASK_STACKSIZE 4 * 1024
#define SPRINKLER_TASK_NAME      "hap_sprinkler"

/* The button "Boot" will be used as the Reset button for the example */
#define RESET_GPIO GPIO_NUM_0

#define IO_RES_PIN 25
#define IO_INT_PIN 26

// Placeholder: button-triggered network/factory reset not yet implemented
static void reset_key_init(uint32_t key_gpio_pin)
{
	(void)key_gpio_pin;
}

/* Mandatory identify routine for the accessory.
 * In a real accessory, something like LED blink should be implemented
 * got visual identification
 */
static int sprinkler_identify(hap_acc_t* ha)
{
	ESP_LOGI(TAG, "Accessory identified");
	return HAP_SUCCESS;
}

/*
 * An optional HomeKit Event handler which can be used to track HomeKit
 * specific events.
 */
static void sprinkler_hap_event_handler(void* arg, esp_event_base_t event_base, int32_t event, void* data)
{
	switch (event)
	{
		case HAP_EVENT_PAIRING_STARTED:
			ESP_LOGI(TAG, "Pairing Started");
			break;
		case HAP_EVENT_PAIRING_ABORTED:
			ESP_LOGI(TAG, "Pairing Aborted");
			break;
		case HAP_EVENT_CTRL_PAIRED:
			ESP_LOGI(TAG, "Controller %s Paired. Controller count: %d", (char*)data, hap_get_paired_controller_count());
			break;
		case HAP_EVENT_CTRL_UNPAIRED:
			ESP_LOGI(TAG, "Controller %s Removed. Controller count: %d", (char*)data,
			         hap_get_paired_controller_count());
			break;
		case HAP_EVENT_CTRL_CONNECTED:
			ESP_LOGI(TAG, "Controller %s Connected", (char*)data);
			break;
		case HAP_EVENT_CTRL_DISCONNECTED:
			ESP_LOGI(TAG, "Controller %s Disconnected", (char*)data);
			break;
		case HAP_EVENT_ACC_REBOOTING:
		{
			char* reason = (char*)data;
			ESP_LOGI(TAG, "Accessory Rebooting (Reason: %s)", reason ? reason : "null");
		}
		break;
		case HAP_EVENT_PAIRING_MODE_TIMED_OUT:
			ESP_LOGI(TAG, "Pairing Mode timed out.");
			menu_ctx.hap.pairing_timed_out = true;
			break;
		default:
			/* Silently ignore unknown events */
			break;
	}
}

static bool ioexp_init(i2c_master_bus_handle_t i2c_port)
{
	ioexp_p = std::make_shared<MCP23017>(i2c_port, MCP23017_BASE_ADDRESS, IO_RES_PIN, IO_INT_PIN, -1, true);
	if (ioexp_p->init() == false)
		return false;
	uint16_t input_cfg = (BTN_SEL_IOEXP_MASK | BTN_BACK_IOEXP_MASK | BTN_UP_IOEXP_MASK | BTN_DN_IOEXP_MASK |
	                      BTN_CH1_IOEXP_MASK | BTN_CH2_IOEXP_MASK);
	ESP_LOGI(TAG, "ioexp init ok");
	if (ioexp_p->setIODIR(input_cfg) == false)
		return false;
	ESP_LOGI(TAG, "ioexp iodir ok");
	if (ioexp_p->setPullUp(input_cfg) == false)
		return false;
	ESP_LOGI(TAG, "ioexp pullup ok");
	if (ioexp_p->setGPIO(0x0000) == false)
		return false;
	ESP_LOGI(TAG, "ioexp gpio ok");
	if (ioexp_p->setIntDefaultEnable(0x0000) == false)
		return false;
	ESP_LOGI(TAG, "ioexp def ena ok");
	if (ioexp_p->setIntEna(input_cfg) == false)
		return false;
	ESP_LOGI(TAG, "int ena ok");
	ioexp_p->regDump();
	return true;
}

/*The main thread for handling the Fan Accessory */
static void sprinkler_thread_entry(void* p)
{
	esp_err_t ret = ESP_OK;

	ret = nvs_flash_init();
	if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND)
	{
		ESP_ERROR_CHECK(nvs_flash_erase());
		ret = nvs_flash_init();
	}
	if (ret != ESP_OK)
	{
		ESP_LOGE(TAG, "nvm init fail %d", ret);
		return;
	}

	// Create valve instances early so menu_ctx.out_ch[] is valid before UI creation
	s_valves[0] = std::make_unique<ValveChannel>(0, "Zone 1", "CH1");
	s_valves[1] = std::make_unique<ValveChannel>(1, "Zone 2", "CH2");
	s_valves[0]->load();
	s_valves[1]->load();
	menu_ctx.out_ch[0] = s_valves[0].get();
	menu_ctx.out_ch[1] = s_valves[1].get();

	// Load saved timezone index, apply before WiFi/NTP starts
	{
		nvs_handle_t h;
		if (nvs_open("storage", NVS_READONLY, &h) == ESP_OK)
		{
			nvs_get_i32(h, "tz_idx", &s_tz_idx);
			nvs_close(h);
		}
		setenv("TZ", timezone_posix((int)s_tz_idx), 1);
		tzset();
	}

	i2c_master_bus_config_t i2c_bus_config = {.i2c_port = -1,
	                                          .sda_io_num = I2C_SDA_PIN,
	                                          .scl_io_num = I2C_SCL_PIN,
	                                          .clk_source = I2C_CLK_SRC_DEFAULT,
	                                          .glitch_ignore_cnt = 7,
	                                          .intr_priority = 0,
	                                          .trans_queue_depth = 0,
	                                          .flags = {.enable_internal_pullup = 1, .allow_pd = 0}};
	i2c_master_bus_handle_t bus_handle;

	ret = i2c_new_master_bus(&i2c_bus_config, &bus_handle);
	if (ret != ESP_OK)
	{
		ESP_LOGE(TAG, "i2c param failed %d", ret);
		return;
	}
	ESP_LOGI(TAG, "i2c bus init ok");

	if (ioexp_init(bus_handle) == false)
	{
		ESP_LOGE(TAG, "ioexp init failed");
	}

	// Wire valve output callbacks to IO expander GPIO pins (read-modify-write to preserve other pins)
	s_valves[0]->setOutputFn(
	    [](bool on)
	    {
		    if (!ioexp_p)
			    return;
		    uint16_t latch = 0;
		    ioexp_p->getOLAT(&latch);
		    if (on)
			    latch |= VALVE_CH0_IOEXP_MASK;
		    else
			    latch &= ~VALVE_CH0_IOEXP_MASK;
		    ioexp_p->setGPIO(latch);
	    });
	s_valves[1]->setOutputFn(
	    [](bool on)
	    {
		    if (!ioexp_p)
			    return;
		    uint16_t latch = 0;
		    ioexp_p->getOLAT(&latch);
		    if (on)
			    latch |= VALVE_CH1_IOEXP_MASK;
		    else
			    latch &= ~VALVE_CH1_IOEXP_MASK;
		    ioexp_p->setGPIO(latch);
	    });
	disp_p = std::make_unique<SSD1306I2C>(0x3C, bus_handle, GEOMETRY_128_64);
	if (disp_p->init())
	{
		disp_p->setContrast(255);
		disp_p->flipScreenVertically();
		disp_p->setLogBuffer(5, 30);
		ui_p = std::make_unique<UI>(*disp_p, ioexp_p, menu_ctx);
		moisture_p = std::make_unique<MoistInput>(MOISTURE_ADC_UNIT, MOISTURE_ADC_CHANNEL);
		moisture_p->load();
	}

	hap_acc_t* accessory;

	/* Configure HomeKit core to make the Accessory name (and thus the WAC SSID) unique,
	 * instead of the default configuration wherein only the WAC SSID is made unique.
	 */
	hap_cfg_t hap_cfg;
	hap_get_config(&hap_cfg);
	hap_cfg.unique_param = UNIQUE_NAME;
	hap_set_config(&hap_cfg);

	/* Initialize the HAP core */
	hap_init(HAP_TRANSPORT_WIFI);

	/* Initialise the mandatory parameters for Accessory which will be added as
	 * the mandatory services internally
	 */
	hap_acc_cfg_t cfg = {
	    .name = (char*)"Sprinkler",
	    .model = (char*)"S1",
	    .manufacturer = (char*)"Gormack",
	    .serial_num = (char*)"001122334455",
	    .fw_rev = (char*)"1.0.0",
	    .hw_rev = NULL,
	    .pv = (char*)"1.0.0",
	    .cid = HAP_CID_SPRINKLER,
	    .identify_routine = sprinkler_identify,
	};
	/* Create accessory object */
	accessory = hap_acc_create(&cfg);

	/* Add a dummy Product Data */
	uint8_t product_data[] = {'E', 'S', 'P', '3', '2', 'H', 'A', 'P'};
	hap_acc_add_product_data(accessory, product_data, sizeof(product_data));

	/* Add Wi-Fi Transport service required for HAP Spec R16 */
	hap_acc_add_wifi_transport_service(accessory, 0);

	/* Wire moisture getter, threshold, and scheduler into valve channels, then create HAP services */
	s_valves[0]->setMoistureGetter([&]() -> float { return moisture_p ? (float)moisture_p->getMoisture() : 0.0f; });
	s_valves[0]->setMoistureThreshold([&]() -> uint8_t { return moisture_p ? moisture_p->getThreshold() : 50; });
	s_valves[0]->registerWithScheduler(s_sched);
	hap_acc_add_serv(accessory, s_valves[0]->createService());

	s_valves[1]->setMoistureThreshold([&]() -> uint8_t { return moisture_p ? moisture_p->getThreshold() : 50; });
	s_valves[1]->registerWithScheduler(s_sched);
	hap_acc_add_serv(accessory, s_valves[1]->createService());

	/* Create the Firmware Upgrade HomeKit Custom Service.
	 * Please refer the FW Upgrade documentation under components/homekit/extras/include/hap_fw_upgrade.h
	 * and the top level README for more information.
	 */
	hap_fw_upgrade_config_t ota_config = {
	    .server_cert_pem = server_cert,
	};
	hap_serv_t* fwservice = hap_serv_fw_upgrade_create(&ota_config);
	/* Add the service to the Accessory Object */
	hap_acc_add_serv(accessory, fwservice);

	/* Add the Accessory to the HomeKit Database */
	hap_add_accessory(accessory);

	/* Register a common button for reset Wi-Fi network and reset to factory.
	 */
	reset_key_init(RESET_GPIO);

	/* Query the controller count (just for information) */
	ESP_LOGI(TAG, "Accessory is paired with %d controllers", hap_get_paired_controller_count());

	/* TODO: Do the actual hardware initialization here */

	/* For production accessories, the setup code shouldn't be programmed on to
	 * the device. Instead, the setup info, derived from the setup code must
	 * be used. Use the factory_nvs_gen utility to generate this data and then
	 * flash it into the factory NVS partition.
	 *
	 * By default, the setup ID and setup info will be read from the factory_nvs
	 * Flash partition and so, is not required to set here explicitly.
	 *
	 * However, for testing purpose, this can be overridden by using hap_set_setup_code()
	 * and hap_set_setup_id() APIs, as has been done here.
	 */
#ifdef CONFIG_EXAMPLE_USE_HARDCODED_SETUP_CODE
	/* Unique Setup code of the format xxx-xx-xxx. Default: 111-22-333 */
	hap_set_setup_code(CONFIG_EXAMPLE_SETUP_CODE);
	/* Unique four character Setup Id. Default: ES32 */
	hap_set_setup_id(CONFIG_EXAMPLE_SETUP_ID);
#ifdef CONFIG_APP_WIFI_USE_WAC_PROVISIONING
	app_hap_setup_payload(CONFIG_EXAMPLE_SETUP_CODE, CONFIG_EXAMPLE_SETUP_ID, true, cfg.cid);
	{
		char* uri = esp_hap_get_setup_payload(CONFIG_EXAMPLE_SETUP_CODE, CONFIG_EXAMPLE_SETUP_ID, true, cfg.cid);
		if (uri)
		{
			menu_ctx.hap.setup_uri = uri;
			free(uri);
		}
	}
#else
	app_hap_setup_payload(CONFIG_EXAMPLE_SETUP_CODE, CONFIG_EXAMPLE_SETUP_ID, false, cfg.cid);
	{
		char* uri = esp_hap_get_setup_payload(CONFIG_EXAMPLE_SETUP_CODE, CONFIG_EXAMPLE_SETUP_ID, false, cfg.cid);
		if (uri)
		{
			menu_ctx.hap.setup_uri = uri;
			free(uri);
		}
	}
#endif
#endif

	/* Enable Hardware MFi authentication (applicable only for MFi variant of SDK) */
	hap_enable_mfi_auth(HAP_MFI_AUTH_HW);

	/* Initialize Wi-Fi */
	app_wifi_handler_init();

	/* Start shared HTTP server then register OTA and control handlers */
	s_web_base.start();
	s_ota_server.start(s_web_base.getHandle());

	/* Start control web server (port 8081) */
	s_ctrl_server.valve_status = [](int ch) -> ControlServer::ValveStatus
	{
		if (!s_valves[ch]) return {false, 0};
		return {s_valves[ch]->isActive(), s_valves[ch]->getRemaining()};
	};
	s_ctrl_server.valve_run  = [](int ch, uint32_t dur_s) { if (s_valves[ch]) s_valves[ch]->manualRun(dur_s); };
	s_ctrl_server.valve_stop = [](int ch)                 { if (s_valves[ch]) s_valves[ch]->manualStop(); };
	s_ctrl_server.moisture   = [&]() -> uint8_t { return moisture_p ? moisture_p->getMoisture() : 0; };
	s_ctrl_server.sched_get  = [](int ch) -> const time_evt_t *
	{
		return s_valves[ch] ? s_valves[ch]->getEvents() : nullptr;
	};
	s_ctrl_server.sched_set  = [](int ch, const time_evt_t *evts)
	{
		if (!s_valves[ch]) return;
		for (int i = 0; i < EVT_PER_CH; i++)
			s_valves[ch]->setEvent(i, evts[i]);
		s_valves[ch]->save(); // scheduler already holds pointers into m_evt, no re-register needed
	};
	s_ctrl_server.start(s_web_base.getHandle());

	/* Wire wifi callbacks into menu context for WifiProvScreen */
	menu_ctx.wifi.start_prov = []() { return wifi_handler_start_provisioning(); };
	menu_ctx.wifi.stop_prov = []() { wifi_handler_stop_provisioning(); };
	menu_ctx.wifi.prov_status = []() -> MenuCtx::ProvStatus
	{
		if (wifi_handler_prov_failed())
			return MenuCtx::ProvStatus::Failed;
		if (wifi_handler_is_connected())
			return MenuCtx::ProvStatus::Connected;
		return MenuCtx::ProvStatus::InProgress;
	};
	menu_ctx.wifi.wifi_info = []() -> MenuCtx::WifiInfo
	{
		MenuCtx::WifiInfo info{};
		wifi_handler_get_info(info.ssid, sizeof(info.ssid), info.ip, sizeof(info.ip), &info.connected);
		return info;
	};
	menu_ctx.wifi.rssi_getter = []() -> int8_t
	{
		wifi_ap_record_t ap_info;
		int8_t           rssi = (esp_wifi_sta_get_ap_info(&ap_info) == ESP_OK) ? ap_info.rssi : 0;
		ESP_LOGI(TAG, "rssi %d", rssi);
		return rssi;
	};
	menu_ctx.settings.tz_getter = []() -> int
	{
		nvs_handle_t h;
		int32_t      idx = 0;
		if (nvs_open("storage", NVS_READONLY, &h) == ESP_OK)
		{
			nvs_get_i32(h, "tz_idx", &idx);
			nvs_close(h);
		}
		return (int)idx;
	};
	menu_ctx.moisture.getter     = [&]() -> uint8_t { return moisture_p ? moisture_p->getMoisture() : 0; };
	menu_ctx.moisture.raw_getter = [&]() -> int      { return moisture_p ? moisture_p->getAvgRaw()    : -1; };
	menu_ctx.moisture.cal_setter = [&](int dry, int wet) { if (moisture_p) moisture_p->setCal(dry, wet); };
	menu_ctx.moisture.thr_getter = [&]() -> uint8_t  { return moisture_p ? moisture_p->getThreshold() : 50; };
	menu_ctx.moisture.thr_setter = [&](uint8_t val)  { if (moisture_p) moisture_p->setThreshold(val); };
	menu_ctx.valves.active = [](int ch) -> bool { return s_valves[ch] ? s_valves[ch]->isActive() : false; };
	menu_ctx.valves.toggle = [](int ch, uint32_t dur_s)
	{
		if (!s_valves[ch]) return;
		if (s_valves[ch]->isActive())
			s_valves[ch]->manualStop();
		else
			s_valves[ch]->manualRun(dur_s);
	};
	menu_ctx.valves.remaining = [](int ch) -> uint32_t { return s_valves[ch] ? s_valves[ch]->getRemaining() : 0; };
	menu_ctx.hap.is_paired = []() -> bool { return hap_get_paired_controller_count() > 0; };
	menu_ctx.hap.reopen_pairing = []()
	{
		ESP_LOGI(TAG, "Reopening HAP pairing window");
		menu_ctx.hap.pairing_timed_out = false;
		hap_pair_setup_re_enable();
	};
	menu_ctx.hap.reset_pairings = []() { hap_reset_pairings(); };
	menu_ctx.settings.tz_setter = [](int idx)
	{
		setenv("TZ", timezone_posix(idx), 1);
		tzset();
		nvs_handle_t h;
		if (nvs_open("storage", NVS_READWRITE, &h) == ESP_OK)
		{
			nvs_set_i32(h, "tz_idx", (int32_t)idx);
			nvs_commit(h);
			nvs_close(h);
		}
	};
	/* Register an event handler for HomeKit specific events.
	 * All event handlers should be registered only after app_wifi_init()
	 */
	esp_event_handler_register(HAP_EVENT, ESP_EVENT_ANY_ID, &sprinkler_hap_event_handler, NULL);

	/* Start the schedule checker */
	s_sched.start();

	/* After all the initializations are done, start the HAP core */
	hap_start();
	/* The task ends here. The read/write callbacks will be invoked by the HAP Framework */
	vTaskDelete(NULL);
}

extern "C" void app_main()
{
	xTaskCreate(sprinkler_thread_entry, SPRINKLER_TASK_NAME, SPRINKLER_TASK_STACKSIZE, NULL, SPRINKLER_TASK_PRIORITY,
	            NULL);
}
