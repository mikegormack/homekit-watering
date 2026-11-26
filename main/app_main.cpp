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
#include <vector>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <esp_timer.h>
#include <esp_event.h>
#include <esp_log.h>
#include <esp_app_desc.h>
#include <nvs_flash.h>
#include <nvs.h>

#include <driver/i2c.h>

#include <hap.h>
#include <hap_apple_servs.h>
#include <hap_apple_chars.h>

#include <hap_fw_upgrade.h>
#include <esp_wifi.h>
#include <app_hap_setup_payload.h>
#include <app_wifi_handler.h>

extern const char* timezone_posix(int idx); // defined in TimezoneScreen.cpp

#include "ValveChannel.h"
#include "Scheduler.h"
#include "OtaServer.h"
#include "ControlServer.h"
#include "WebBase.h"
#include "EventLog.h"
#include "MoistureTrend.h"
#include "ProbeServer.h"
// WebBase owns the httpd instance; OtaServer, ControlServer and ProbeServer register onto it
#include <ProbeManager.h>
#include <UI.h>

#include <SSD1306I2C.h>
#include <MCP23017.h>

#include <iocfg.h>

/*  Required for server verification during OTA, PEM format as string  */
char server_cert[] = {};

static std::unique_ptr<ValveChannel> s_valves[2];
static Scheduler                     s_sched;
static EventLog                      s_event_log;
static MoistureTrend                 s_mtrend;
static WebBase                       s_web_base;
static OtaServer                     s_ota_server;
static ControlServer                 s_ctrl_server;
static ProbeManager                  s_probes;
static ProbeServer                   s_probe_server(s_probes);

static std::unique_ptr<SSD1306I2C> disp_p;
static std::unique_ptr<UI>         ui_p;
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

/* HomeKit services for the soil probes configured at boot: a humidity sensor (soil moisture) and a
 * temperature sensor per probe. The HomeKit database is fixed once HAP starts, so probes added or
 * removed later appear after a restart. Entries keep their slot; only the address can change. */
struct ProbeHap
{
	uint8_t     addr;
	hap_serv_t *hum;
	hap_serv_t *temp;
};
static std::vector<ProbeHap> s_probe_hap;

/* ---- Irrigation System service ------------------------------------------------------------
 * Primary service with both valves linked to it, so the Home app shows one sprinkler.
 *   Active      : on while any zone is in automatic mode or running. Writing off stops running
 *                 zones and turns automatic mode off for every zone; writing on turns it back on.
 *   ProgramMode : 1 (scheduled) while any zone is in automatic mode, else 0.
 *   InUse       : 1 while any zone is running.
 * Per-zone automatic mode also has its own "<zone> Auto" switch (ValveChannel). */
static hap_serv_t *s_irrigation = nullptr;

/* Services added after the original ones get fixed instance IDs from here, so neither they nor
 * the existing services are renumbered when probe sensors are added or removed (HomeKit keeps
 * their names, rooms and automations). */
#define HAP_IID_FIXED_BASE 2000

static void set_fixed_iids(hap_serv_t *hs, int32_t &next)
{
	hap_serv_set_iid(hs, next++);
	for (hap_char_t *hc = hap_serv_get_first_char(hs); hc; hc = hap_char_get_next(hc))
		hap_char_set_iid(hc, next++);
}

static bool any_zone_auto()
{
	for (auto &v : s_valves)
		if (v && v->isSchedEnabled())
			return true;
	return false;
}

static bool any_zone_running()
{
	for (auto &v : s_valves)
		if (v && v->isActive())
			return true;
	return false;
}

static void irrigation_set(hap_char_t *hc)
{
	if (!hc)
		return;
	const char *uuid = hap_char_get_type_uuid(hc);
	hap_val_t   val;
	if (!strcmp(uuid, HAP_CHAR_UUID_ACTIVE))
		val.u = (any_zone_auto() || any_zone_running()) ? 1 : 0;
	else if (!strcmp(uuid, HAP_CHAR_UUID_PROGRAM_MODE))
		val.u = any_zone_auto() ? 1 : 0;
	else if (!strcmp(uuid, HAP_CHAR_UUID_IN_USE))
		val.u = any_zone_running() ? 1 : 0;
	else
		return;
	hap_char_update_val(hc, &val);
}

/* Called whenever a zone starts/stops or its automatic mode changes. */
static void irrigation_update()
{
	if (!s_irrigation)
		return;
	irrigation_set(hap_serv_get_char_by_uuid(s_irrigation, HAP_CHAR_UUID_ACTIVE));
	irrigation_set(hap_serv_get_char_by_uuid(s_irrigation, HAP_CHAR_UUID_PROGRAM_MODE));
	irrigation_set(hap_serv_get_char_by_uuid(s_irrigation, HAP_CHAR_UUID_IN_USE));
}

static int irrigation_read_cb(hap_char_t *hc, hap_status_t *status, void *serv_priv, void *read_priv)
{
	irrigation_set(hc);
	*status = HAP_STATUS_SUCCESS;
	return HAP_SUCCESS;
}

static int irrigation_write_cb(hap_write_data_t *write_data, int count, void *serv_priv, void *write_priv)
{
	for (int i = 0; i < count; i++)
	{
		hap_write_data_t *w = &write_data[i];
		if (strcmp(hap_char_get_type_uuid(w->hc), HAP_CHAR_UUID_ACTIVE))
		{
			*(w->status) = HAP_STATUS_RES_ABSENT;
			continue;
		}
		bool on = w->val.u != 0;
		ESP_LOGI(TAG, "HomeKit: sprinkler %s", on ? "on (automatic mode on)" : "off (stop, automatic mode off)");
		for (auto &v : s_valves)
		{
			if (!v)
				continue;
			if (!on && v->isActive())
				v->manualStop();
			if (v->isSchedEnabled() != on)
			{
				v->setSchedEnabled(on);  // updates the zone's switch and this service
				v->save();
			}
		}
		hap_char_update_val(w->hc, &w->val);
		*(w->status) = HAP_STATUS_SUCCESS;
	}
	return HAP_SUCCESS;
}

/* Soil temperature from a zone's probe; false when the zone has no probe or no usable reading. */
static bool zone_soil_temp(int ch, float *temp_c)
{
	return s_valves[ch] && s_probes.temperature(s_valves[ch]->getProbeAddr(), temp_c);
}

/* Writes the probe's current state into one characteristic (notifies controllers if it changed). */
static void probe_hap_set(hap_char_t *hc, const ProbeManager::Probe *p, bool is_temp)
{
	if (!hc)
		return;
	const ProbeManager::Reading *r    = p ? &p->r : nullptr;
	bool                         live = r && r->online && !r->in_bootloader;
	bool moist_ok = live && !(r->status & probe_reg::ST_UNCALIBRATED);
	bool temp_ok  = live && !(r->status & probe_reg::ST_NTC_FAULT);
	const char *uuid = hap_char_get_type_uuid(hc);

	hap_val_t v;
	if (!strcmp(uuid, HAP_CHAR_UUID_CURRENT_RELATIVE_HUMIDITY))
	{
		float m = moist_ok ? r->moisture / 100.0f : 0.0f;
		v.f     = m < 0 ? 0 : m > 100 ? 100 : m;
	}
	else if (!strcmp(uuid, HAP_CHAR_UUID_CURRENT_TEMPERATURE))
		v.f = temp_ok ? r->temp / 100.0f : 0.0f;
	else if (!strcmp(uuid, HAP_CHAR_UUID_STATUS_ACTIVE))
		v.b = live;
	else if (!strcmp(uuid, HAP_CHAR_UUID_STATUS_FAULT))
		v.u = (is_temp ? temp_ok : moist_ok) ? 0 : 1;
	else
		return;
	hap_char_update_val(hc, &v);
}

static void probe_hap_update(const ProbeHap &ph, const ProbeManager::Probe *p)
{
	static const char *uuids[] = {HAP_CHAR_UUID_CURRENT_RELATIVE_HUMIDITY, HAP_CHAR_UUID_CURRENT_TEMPERATURE,
	                              HAP_CHAR_UUID_STATUS_ACTIVE, HAP_CHAR_UUID_STATUS_FAULT};
	for (const char *u : uuids)
	{
		probe_hap_set(hap_serv_get_char_by_uuid(ph.hum, u), p, false);
		probe_hap_set(hap_serv_get_char_by_uuid(ph.temp, u), p, true);
	}
}

static int probe_hap_read_cb(hap_char_t *hc, hap_status_t *status, void *serv_priv, void *read_priv)
{
	const ProbeHap     *ph = static_cast<const ProbeHap *>(serv_priv);
	ProbeManager::Probe p;
	bool                have = ph && s_probes.get(ph->addr, p);
	probe_hap_set(hc, have ? &p : nullptr, ph && hap_char_get_parent(hc) == ph->temp);
	*status = HAP_STATUS_SUCCESS;
	return HAP_SUCCESS;
}

/* Creates the probe services; returns a signature of the probe set so the HomeKit configuration
 * number can be bumped when it changes between boots. */
static uint32_t probe_hap_create(hap_acc_t *accessory)
{
	std::vector<ProbeManager::Probe> probes = s_probes.list();
	s_probe_hap.reserve(ProbeManager::MAX_PROBES);  // entries are serv_priv pointers: never reallocate
	uint32_t sig = 0x811C9DC5u;                     // FNV-1a over the addresses
	for (const auto &p : probes)
	{
		sig = (sig ^ p.addr) * 0x01000193u;

		ProbeHap ph = {p.addr, hap_serv_humidity_sensor_create(0.0f), hap_serv_temperature_sensor_create(0.0f)};
		char     name[ProbeManager::NAME_LEN + 8];
		snprintf(name, sizeof(name), "%s", p.name);
		hap_serv_add_char(ph.hum, hap_char_name_create(name));
		hap_serv_add_char(ph.hum, hap_char_status_active_create(false));
		hap_serv_add_char(ph.hum, hap_char_status_fault_create(0));
		snprintf(name, sizeof(name), "%s Temp", p.name);
		hap_serv_add_char(ph.temp, hap_char_name_create(name));
		hap_serv_add_char(ph.temp, hap_char_status_active_create(false));
		hap_serv_add_char(ph.temp, hap_char_status_fault_create(0));
		hap_char_float_set_constraints(hap_serv_get_char_by_uuid(ph.temp, HAP_CHAR_UUID_CURRENT_TEMPERATURE), -40.0f,
		                               100.0f, 0.1f);

		s_probe_hap.push_back(ph);
		ProbeHap *slot = &s_probe_hap.back();
		hap_serv_set_priv(ph.hum, slot);
		hap_serv_set_priv(ph.temp, slot);
		hap_serv_set_read_cb(ph.hum, probe_hap_read_cb);
		hap_serv_set_read_cb(ph.temp, probe_hap_read_cb);
		hap_acc_add_serv(accessory, ph.hum);
		hap_acc_add_serv(accessory, ph.temp);
	}
	ESP_LOGI(TAG, "%d probe(s) in HomeKit", (int)s_probe_hap.size());
	return sig;
}

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

	s_event_log.load();
	s_mtrend.load();
	{
		esp_reset_reason_t reason = esp_reset_reason();
		bool is_crash = (reason == ESP_RST_PANIC   || reason == ESP_RST_WDT ||
		                 reason == ESP_RST_TASK_WDT || reason == ESP_RST_INT_WDT);
		s_event_log.add(0xFF, is_crash ? LogEvt::CRASH : LogEvt::BOOT, (uint16_t)reason);
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
	}

	/* Soil probe list (NVS); the bus and polling start after the HomeKit services exist */
	s_probes.load();

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
	    .fw_rev = (char*)esp_app_get_description()->version,
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

	/* Wire probe moisture and scheduler into valve channels, then create HAP services */
	for (auto &v : s_valves)
	{
		v->setMoistureGetter([](uint8_t addr) -> float { return s_probes.moisture(addr); });
		v->setEventLog(s_event_log);
		v->registerWithScheduler(s_sched);
		hap_acc_add_serv(accessory, v->createService());
	}

	/* Soil probes: humidity + temperature sensor per configured probe */
	uint32_t probe_sig = probe_hap_create(accessory);

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

	/* Irrigation System (primary, valves linked) and per-zone automatic-mode switches. Added
	 * last with fixed instance IDs; see HAP_IID_FIXED_BASE. */
	{
		int32_t iid  = HAP_IID_FIXED_BASE;
		s_irrigation = hap_serv_irrigation_system_create(any_zone_auto() ? 1 : 0, any_zone_auto() ? 1 : 0, 0);
		hap_serv_add_char(s_irrigation, hap_char_name_create((char *)"Sprinkler"));
		hap_serv_mark_primary(s_irrigation);
		hap_serv_set_read_cb(s_irrigation, irrigation_read_cb);
		hap_serv_set_write_cb(s_irrigation, irrigation_write_cb);
		hap_acc_add_serv(accessory, s_irrigation);
		for (auto &v : s_valves)
			hap_serv_link_serv(s_irrigation, v->service());
		set_fixed_iids(s_irrigation, iid);

		for (auto &v : s_valves)
		{
			hap_serv_t *sw = v->createAutoSwitchService();
			hap_acc_add_serv(accessory, sw);
			set_fixed_iids(sw, iid);
			v->setRunChangedFn(irrigation_update);
			v->setAutoChangedFn(irrigation_update);
		}
	}

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

	/* Start the RS485 probe bus and polling */
	s_probes.on_reading = [](const ProbeManager::Probe &p)
	{
		for (const auto &ph : s_probe_hap)
			if (ph.addr == p.addr)
				probe_hap_update(ph, &p);
	};
	s_probes.on_event = [](ProbeManager::Event ev, uint8_t addr, uint16_t val)
	{
		switch (ev)
		{
			case ProbeManager::Event::OFFLINE: s_event_log.add(0xFF, LogEvt::PROBE_OFFLINE, addr); break;
			case ProbeManager::Event::ONLINE:  s_event_log.add(0xFF, LogEvt::PROBE_ONLINE, addr); break;
			case ProbeManager::Event::FW_UPDATED:
				s_event_log.add(0xFF, LogEvt::PROBE_FW,
				                (uint16_t)(addr << 8 | ((val >> 8) & 0xF) << 4 | (val & 0xF)));
				break;
		}
	};
	s_probes.on_readdress = [](uint8_t old_addr, uint8_t new_addr)
	{
		for (auto &v : s_valves)
			if (v && v->getProbeAddr() == old_addr)
			{
				v->setProbeAddr(new_addr);
				v->save();
			}
		for (auto &ph : s_probe_hap)
			if (ph.addr == old_addr)
				ph.addr = new_addr;
	};
	{
		ModbusMaster::Config bus = {
		    .uart    = (uart_port_t)CONFIG_PROBE_UART_NUM,
		    .tx_gpio = CONFIG_PROBE_UART_TX_GPIO,
		    .rx_gpio = CONFIG_PROBE_UART_RX_GPIO,
		    .de_gpio = CONFIG_PROBE_UART_DE_GPIO,
		    .baud    = CONFIG_PROBE_BAUD,
		};
		if (!s_probes.begin(bus, CONFIG_PROBE_POLL_INTERVAL_S))
			ESP_LOGE(TAG, "RS485 probe bus init failed");
	}

	/* HomeKit pairing details for the web page (/homekit) */
	s_web_base.homekit_info = []() -> WebBase::HomeKitInfo
	{
		WebBase::HomeKitInfo info;
		info.setup_uri = menu_ctx.hap.setup_uri;
#ifdef CONFIG_EXAMPLE_USE_HARDCODED_SETUP_CODE
		info.setup_code = CONFIG_EXAMPLE_SETUP_CODE;
#endif
		info.paired            = hap_get_paired_controller_count();
		info.pairing_timed_out = menu_ctx.hap.pairing_timed_out;
		return info;
	};
	s_web_base.homekit_reset_pairings = []() { hap_reset_pairings(); };  // reboots the accessory
	s_web_base.homekit_reopen_pairing = []()
	{
		ESP_LOGI(TAG, "Reopening HAP pairing window (web)");
		menu_ctx.hap.pairing_timed_out = false;
		hap_pair_setup_re_enable();
	};

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
	s_ctrl_server.moisture   = [](int ch) -> float { return s_valves[ch] ? s_valves[ch]->moisture() : -1.0f; };
	s_ctrl_server.soil_temp  = [](int ch, float *t) -> bool { return zone_soil_temp(ch, t); };
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
	s_ctrl_server.sched_enabled_get = [](int ch) -> bool
	{
		return s_valves[ch] ? s_valves[ch]->isSchedEnabled() : true;
	};
	s_ctrl_server.sched_enabled_set = [](int ch, bool enabled)
	{
		if (!s_valves[ch]) return;
		s_valves[ch]->setSchedEnabled(enabled);
		s_valves[ch]->save();
	};
	s_ctrl_server.log_get   = [](char *buf, int len) { return s_event_log.toJson(buf, len); };
	s_ctrl_server.log_clear = []() { s_event_log.clear(); };
	s_ctrl_server.trend_get   = [](char *buf, int len) { return s_mtrend.toJson(buf, len); };
	s_ctrl_server.trend_clear = []() { s_mtrend.clear(); };
	s_ota_server.on_ota_complete = [](uint16_t ver) { s_event_log.add(0xFF, LogEvt::OTA_COMPLETE, ver); };
	s_ota_server.partition_busy  = []() { return s_probes.busy(); };
	s_ctrl_server.start(s_web_base.getHandle());

	s_probe_server.zone_get = [](int ch) -> ProbeServer::Zone
	{
		if (!s_valves[ch]) return {0, 50};
		return {s_valves[ch]->getProbeAddr(), s_valves[ch]->getMoistThreshold()};
	};
	s_probe_server.zone_set = [](int ch, const ProbeServer::Zone &z)
	{
		if (!s_valves[ch]) return;
		s_valves[ch]->setProbeAddr(z.probe_addr);
		s_valves[ch]->setMoistThreshold(z.threshold);
		s_valves[ch]->save();
	};
	s_probe_server.start(s_web_base.getHandle());

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
	menu_ctx.probes.ch_moisture = [](int ch) -> float { return s_valves[ch] ? s_valves[ch]->moisture() : -1.0f; };
	menu_ctx.probes.ch_temp     = [](int ch, float *t) -> bool { return zone_soil_temp(ch, t); };
	menu_ctx.probes.list        = []() -> std::vector<MenuCtx::ProbeSummary>
	{
		std::vector<MenuCtx::ProbeSummary> out;
		for (const auto &p : s_probes.list())
		{
			MenuCtx::ProbeSummary ps{};
			ps.addr = p.addr;
			snprintf(ps.name, sizeof(ps.name), "%s", p.name);
			ps.online        = p.r.online;
			ps.in_bootloader = p.r.in_bootloader;
			ps.calibrated    = !(p.r.status & probe_reg::ST_UNCALIBRATED);
			ps.temp_ok       = !(p.r.status & probe_reg::ST_NTC_FAULT);
			ps.moisture      = p.r.moisture / 100.0f;
			ps.temp          = p.r.temp / 100.0f;
			out.push_back(ps);
		}
		return out;
	};
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

	/* Start moisture trend logging (hourly; each zone's probe) */
	s_mtrend.start([](int ch) -> float { return s_valves[ch] ? s_valves[ch]->moisture() : -1.0f; },
	               [](int ch, float *t) -> bool { return zone_soil_temp(ch, t); });

	/* Start the schedule checker */
	s_sched.start();

	/* After all the initializations are done, start the HAP core */
	hap_start();

	/* HomeKit caches the accessory database by configuration number, which the SDK only bumps when
	 * the firmware version changes. Bump it when the set of probe services changed since last boot. */
	{
		nvs_handle_t h;
		uint32_t     old_sig = 0;
		if (nvs_open("storage", NVS_READWRITE, &h) == ESP_OK)
		{
			nvs_get_u32(h, "hap_probe_sig", &old_sig);
			if (old_sig != probe_sig)
			{
				ESP_LOGI(TAG, "Probe services changed, updating HomeKit configuration number");
				hap_update_config_number();
				nvs_set_u32(h, "hap_probe_sig", probe_sig);
				nvs_commit(h);
			}
			nvs_close(h);
		}
	}
	/* The task ends here. The read/write callbacks will be invoked by the HAP Framework */
	vTaskDelete(NULL);
}

extern "C" void app_main()
{
	xTaskCreate(sprinkler_thread_entry, SPRINKLER_TASK_NAME, SPRINKLER_TASK_STACKSIZE, NULL, SPRINKLER_TASK_PRIORITY,
	            NULL);
}
