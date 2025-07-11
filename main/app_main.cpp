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
#include <memory>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
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

#include <app_wifi.h>
#include <app_hap_setup_payload.h>

#include <OutputChannels.h>
#include <UI.h>

#include <SSD1306I2C.h>
#include <MCP23017.h>

#include <iocfg.h>

/*  Required for server verification during OTA, PEM format as string  */
char server_cert[] = {};

static hap_serv_t *service;

static std::unique_ptr<SSD1306I2C> disp_p;
static std::unique_ptr<UI> ui_p;
static std::shared_ptr<MCP23017> ioexp_p;
static OutputChannels out_ch;

static MenuCtx menu_ctx(out_ch);

static const char *TAG = "HAP Sprinkler";

#define SPRINKLER_TASK_PRIORITY 1
#define SPRINKLER_TASK_STACKSIZE 4 * 1024
#define SPRINKLER_TASK_NAME "hap_sprinkler"

/* Reset network credentials if button is pressed for more than 3 seconds and then released */
#define RESET_NETWORK_BUTTON_TIMEOUT 3

/* Reset to factory if button is pressed and held for more than 10 seconds */
#define RESET_TO_FACTORY_BUTTON_TIMEOUT 10

/* The button "Boot" will be used as the Reset button for the example */
#define RESET_GPIO GPIO_NUM_0

#define IO_RES_PIN 25
#define IO_INT_PIN 26

/**
 * @brief The network reset button callback handler.
 * Useful for testing the Wi-Fi re-configuration feature of WAC2
 */
static void reset_network_handler(void *arg)
{
	hap_reset_network();
}
/**
 * @brief The factory reset button callback handler.
 */
static void reset_to_factory_handler(void *arg)
{
	hap_reset_to_factory();
}

/**
 * The Reset button  GPIO initialisation function.
 * Same button will be used for resetting Wi-Fi network as well as for reset to factory based on
 * the time for which the button is pressed.
 */
static void reset_key_init(uint32_t key_gpio_pin)
{
	/*button_handle_t handle = iot_button_create((gpio_num_t)key_gpio_pin, BUTTON_ACTIVE_LOW);
	iot_button_add_on_release_cb(handle, RESET_NETWORK_BUTTON_TIMEOUT, reset_network_handler, NULL);
	iot_button_add_on_press_cb(handle, RESET_TO_FACTORY_BUTTON_TIMEOUT, reset_to_factory_handler, NULL);*/
}

/* Mandatory identify routine for the accessory.
 * In a real accessory, something like LED blink should be implemented
 * got visual identification
 */
static int sprinkler_identify(hap_acc_t *ha)
{
	ESP_LOGI(TAG, "Accessory identified");
	return HAP_SUCCESS;
}

/*
 * An optional HomeKit Event handler which can be used to track HomeKit
 * specific events.
 */
static void sprinkler_hap_event_handler(void *arg, esp_event_base_t event_base, int32_t event, void *data)
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
		ESP_LOGI(TAG, "Controller %s Paired. Controller count: %d",
				 (char *)data, hap_get_paired_controller_count());
		break;
	case HAP_EVENT_CTRL_UNPAIRED:
		ESP_LOGI(TAG, "Controller %s Removed. Controller count: %d",
				 (char *)data, hap_get_paired_controller_count());
		break;
	case HAP_EVENT_CTRL_CONNECTED:
		ESP_LOGI(TAG, "Controller %s Connected", (char *)data);
		break;
	case HAP_EVENT_CTRL_DISCONNECTED:
		ESP_LOGI(TAG, "Controller %s Disconnected", (char *)data);
		break;
	case HAP_EVENT_ACC_REBOOTING:
	{
		char *reason = (char *)data;
		ESP_LOGI(TAG, "Accessory Rebooting (Reason: %s)", reason ? reason : "null");
	}
	break;
	case HAP_EVENT_PAIRING_MODE_TIMED_OUT:
		ESP_LOGI(TAG, "Pairing Mode timed out. Please reboot the device.");
		break;
	default:
		/* Silently ignore unknown events */
		break;
	}
}

static int sprinkler_read(hap_char_t *hc, hap_status_t *status_code, void *serv_priv, void *read_priv)
{
	if (hap_req_get_ctrl_id(read_priv))
	{
		ESP_LOGI(TAG, "Received read from %s", hap_req_get_ctrl_id(read_priv));
	}
	ESP_LOGI(TAG, "UUID %s", hap_char_get_type_uuid(hc));

	/*if (!strcmp(hap_char_get_type_uuid(hc), HAP_CHAR_UUID_ON))
	{
		*status_code = HAP_STATUS_SUCCESS;
		ESP_LOGI(TAG, "On");
	}*/
	const hap_val_t *cur_val;
	if (!strcmp(hap_char_get_type_uuid(hc), HAP_CHAR_UUID_ACTIVE))
	{
		*status_code = HAP_STATUS_SUCCESS;
		cur_val = hap_char_get_val(hc);
		ESP_LOGI(TAG, "Active: %s", cur_val->b ? "1" : "0");
	}
	else if (!strcmp(hap_char_get_type_uuid(hc), HAP_CHAR_UUID_IN_USE))
	{
		*status_code = HAP_STATUS_SUCCESS;
		cur_val = hap_char_get_val(hc);
		ESP_LOGI(TAG, "In Use: %s", cur_val->b ? "1" : "0");
	}
	else if (!strcmp(hap_char_get_type_uuid(hc), HAP_CHAR_UUID_NAME))
	{
		*status_code = HAP_STATUS_SUCCESS;
		cur_val = hap_char_get_val(hc);
		ESP_LOGI(TAG, "Name: %s", cur_val->s);
	}
	/*else if (!strcmp(hap_char_get_type_uuid(hc), HAP_CHAR_UUID_PROGRAM_MODE))
	{
		*status_code = HAP_STATUS_SUCCESS;
		ESP_LOGI(TAG, "Program Mode");
	}*/
	else if (!strcmp(hap_char_get_type_uuid(hc), HAP_CHAR_UUID_SET_DURATION))
	{
		*status_code = HAP_STATUS_SUCCESS;
		cur_val = hap_char_get_val(hc);
		ESP_LOGI(TAG, "Set Duration: %lu", cur_val->u);
	}
	else if (!strcmp(hap_char_get_type_uuid(hc), HAP_CHAR_UUID_REMAINING_DURATION))
	{
		*status_code = HAP_STATUS_SUCCESS;
		cur_val = hap_char_get_val(hc);
		ESP_LOGI(TAG, "Rem Duration: %lu", cur_val->u);
	}
	else if (!strcmp(hap_char_get_type_uuid(hc), HAP_CHAR_UUID_VALVE_TYPE))
	{
		*status_code = HAP_STATUS_SUCCESS;
		cur_val = hap_char_get_val(hc);
		ESP_LOGI(TAG, "Valve Type: %lu", cur_val->u);
	}
	else if (!strcmp(hap_char_get_type_uuid(hc), HAP_CHAR_UUID_WATER_LEVEL))
	{
		*status_code = HAP_STATUS_SUCCESS;
		cur_val = hap_char_get_val(hc);
		ESP_LOGI(TAG, "Water level: %f", cur_val->f);
	}
	else if (!strcmp(hap_char_get_type_uuid(hc), HAP_CHAR_UUID_CURRENT_RELATIVE_HUMIDITY))
	{
		*status_code = HAP_STATUS_SUCCESS;
		cur_val = hap_char_get_val(hc);
		ESP_LOGI(TAG, "Humidty: %f", cur_val->f);
	}
	/* if (!strcmp(hap_char_get_type_uuid(hc), HAP_CHAR_UUID_ROTATION_DIRECTION)) {

		 const hap_val_t *cur_val = hap_char_get_val(hc);

		 hap_val_t new_val;
		 if (cur_val->i == 1) {
			 new_val.i = 0;
		 } else {
			 new_val.i = 1;
		 }
		 hap_char_update_val(hc, &new_val);
		 *status_code = HAP_STATUS_SUCCESS;
	 }*/
	return HAP_SUCCESS;
}

static int sprinkler_write(hap_write_data_t write_data[], int count,
						   void *serv_priv, void *write_priv)
{
	if (hap_req_get_ctrl_id(write_priv))
	{
		ESP_LOGI(TAG, "Received write from %s", hap_req_get_ctrl_id(write_priv));
	}
	ESP_LOGI(TAG, "Sprinkler write called with %d chars", count);
	int i, ret = HAP_SUCCESS;
	hap_write_data_t *write;
	for (i = 0; i < count; i++)
	{
		write = &write_data[i];
		ESP_LOGI(TAG, "UUID %s", hap_char_get_type_uuid(write->hc));

		if (!strcmp(hap_char_get_type_uuid(write->hc), HAP_CHAR_UUID_ACTIVE))
		{
			hap_char_update_val(write->hc, &(write->val));

			hap_char_t *in_use = hap_serv_get_char_by_uuid(service, HAP_CHAR_UUID_IN_USE);

			ESP_LOGI(TAG, "Srv Addr %lu", (uint32_t)service);
			ESP_LOGI(TAG, "Srv Priv Addr %lu", (uint32_t)serv_priv);
			hap_val_t new_val;

			if (write->val.b)
			{
				ESP_LOGI(TAG, "Turned ON");
			}
			else
			{
				ESP_LOGI(TAG, "Turned OFF");
			}
			new_val.u = write->val.u;
			int res = hap_char_update_val(in_use, &new_val);

			ESP_LOGI(TAG, "Update Char Res %d", res);

			// const hap_val_t *cur_val = hap_char_get_val(in_use);
			// ESP_LOGI(TAG, "In Use: %s", cur_val->b ? "1" : "0");

			*(write->status) = HAP_STATUS_SUCCESS;
		}
		else if (!strcmp(hap_char_get_type_uuid(write->hc), HAP_CHAR_UUID_IN_USE))
		{
			hap_char_update_val(write->hc, &(write->val));
			*(write->status) = HAP_STATUS_SUCCESS;
		}
		else if (!strcmp(hap_char_get_type_uuid(write->hc), HAP_CHAR_UUID_PROGRAM_MODE))
		{
			hap_char_update_val(write->hc, &(write->val));
			*(write->status) = HAP_STATUS_SUCCESS;
		}
		else if (!strcmp(hap_char_get_type_uuid(write->hc), HAP_CHAR_UUID_SET_DURATION))
		{
			hap_char_update_val(write->hc, &(write->val));
			ESP_LOGI(TAG, "Set Duration %lu", write->val.u);

			hap_char_t *rem_dur = hap_serv_get_char_by_uuid(service, HAP_CHAR_UUID_REMAINING_DURATION);
			hap_val_t new_val;
			new_val.u = write->val.u - 100;
			hap_char_update_val(rem_dur, &new_val);

			*(write->status) = HAP_STATUS_SUCCESS;
		}
		else if (!strcmp(hap_char_get_type_uuid(write->hc), HAP_CHAR_UUID_REMAINING_DURATION))
		{
			hap_char_update_val(write->hc, &(write->val));
			*(write->status) = HAP_STATUS_SUCCESS;
		}
		else if (!strcmp(hap_char_get_type_uuid(write->hc), HAP_CHAR_UUID_WATER_LEVEL))
		{
			hap_char_update_val(write->hc, &(write->val));
			*(write->status) = HAP_STATUS_SUCCESS;
		}
		/*else if (!strcmp(hap_char_get_type_uuid(write->hc), HAP_CHAR_UUID_ROTATION_DIRECTION))
		{
			if (write->val.i > 1)
			{
				*(write->status) = HAP_STATUS_VAL_INVALID;
				ret = HAP_FAIL;
			}
			else
			{
				ESP_LOGI(TAG, "Received Write. Fan %s", write->val.i ? "AntiClockwise" : "Clockwise");
				hap_char_update_val(write->hc, &(write->val));
				*(write->status) = HAP_STATUS_SUCCESS;
			}
		} */
		else
		{
			*(write->status) = HAP_STATUS_RES_ABSENT;
		}
	}
	return ret;
}

static bool ioexp_init(i2c_master_bus_handle_t i2c_port)
{
	ioexp_p = std::make_shared<MCP23017>(i2c_port, MCP23017_BASE_ADDRESS, IO_RES_PIN, IO_INT_PIN, -1, true);
	if (ioexp_p->init() == false)
		return false;
	uint16_t input_cfg = (BTN_SEL_IOEXP_MASK | BTN_BACK_IOEXP_MASK | BTN_UP_IOEXP_MASK | BTN_DN_IOEXP_MASK);
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
static void sprinkler_thread_entry(void *p)
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

	out_ch.load();

	i2c_master_bus_config_t i2c_bus_config =
		{
			.i2c_port = -1,
			.sda_io_num = I2C_SDA_PIN,
			.scl_io_num = I2C_SCL_PIN,
			.clk_source = I2C_CLK_SRC_DEFAULT,
			.glitch_ignore_cnt = 7,
			.intr_priority = 0,
			.trans_queue_depth = 0,
			.flags =
				{
					.enable_internal_pullup = 1,
					.allow_pd = 0}};
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
	disp_p = std::make_unique<SSD1306I2C>(0x3C, bus_handle, GEOMETRY_128_64);
	if (disp_p->init())
	{

		disp_p->setContrast(255);
		disp_p->flipScreenVertically();
		disp_p->setLogBuffer(5, 30);
		ui_p = std::make_unique<UI>(*disp_p, ioexp_p, menu_ctx);
		// ui_p->HomeScreen();
		// display.resetDisplay();
		// display.setColor(BLACK);

		/*display.setColor(WHITE);

		//display.fillCircle(20, 20, 10);
		//display.setColor(INVERSE);
		display.drawString(0, 0, "Test");

		display.setFont(ArialMT_Plain_16);
		display.drawString(50, 40, "Test");


		display.setFont(ArialMT_Plain_24);

		display.drawString(50, 20, "Test");

		display.drawBitmap(0, 40, wifi1_icon16x16, 16, 16);
		//display.drawString(0, 20, "trev mc farkleberry");
display.fillRect(30,40, 20, 20);
		display.display();*/
	}

	hap_acc_t *accessory;
	// hap_serv_t *service;

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
		.name = (char *)"Sprinkler",
		.model = (char *)"S1",
		.manufacturer = (char *)"Gormack",
		.serial_num = (char *)"001122334455",
		.fw_rev = (char *)"1.0.0",
		.hw_rev = NULL,
		.pv = (char *)"1.0.0",
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

	/* Create the Fan Service. Include the "name" since this is a user visible service  */
	/*service = hap_serv_valve_create(0, 0, 1);
	hap_serv_add_char(service, hap_char_name_create("My Sprinkler"));
	hap_serv_add_char(service, hap_char_set_duration_create(1000));
	hap_serv_add_char(service, hap_char_remaining_duration_create(1000));*/

	service = hap_serv_humidity_sensor_create(50.5);
	hap_serv_add_char(service, hap_char_name_create((char *)"Water Level"));
	hap_serv_add_char(service, hap_char_water_level_create(75));

	/* Set the write callback for the service */
	hap_serv_set_write_cb(service, sprinkler_write);

	/* Set the read callback for the service (optional) */
	hap_serv_set_read_cb(service, sprinkler_read);

	/* Add the Fan Service to the Accessory Object */
	hap_acc_add_serv(accessory, service);

	/* Create the Firmware Upgrade HomeKit Custom Service.
	 * Please refer the FW Upgrade documentation under components/homekit/extras/include/hap_fw_upgrade.h
	 * and the top level README for more information.
	 */
	hap_fw_upgrade_config_t ota_config = {
		.server_cert_pem = server_cert,
	};
	hap_serv_t *fwservice = hap_serv_fw_upgrade_create(&ota_config);
	/* Add the service to the Accessory Object */
	hap_acc_add_serv(accessory, fwservice);

	/* Add the Accessory to the HomeKit Database */
	hap_add_accessory(accessory);

	/* Register a common button for reset Wi-Fi network and reset to factory.
	 */
	reset_key_init(RESET_GPIO);

	/* Query the controller count (just for information) */
	ESP_LOGI(TAG, "Accessory is paired with %d controllers",
			 hap_get_paired_controller_count());

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
#else
	app_hap_setup_payload(CONFIG_EXAMPLE_SETUP_CODE, CONFIG_EXAMPLE_SETUP_ID, false, cfg.cid);
#endif
#endif

	/* Enable Hardware MFi authentication (applicable only for MFi variant of SDK) */
	hap_enable_mfi_auth(HAP_MFI_AUTH_HW);

	/* Initialize Wi-Fi */
	app_wifi_init();

	/* Register an event handler for HomeKit specific events.
	 * All event handlers should be registered only after app_wifi_init()
	 */
	esp_event_handler_register(HAP_EVENT, ESP_EVENT_ANY_ID, &sprinkler_hap_event_handler, NULL);

	/* After all the initializations are done, start the HAP core */
	// hap_start();
	/* Start Wi-Fi */
	// app_wifi_start(portMAX_DELAY);
	/* The task ends here. The read/write callbacks will be invoked by the HAP Framework */
	vTaskDelete(NULL);
}

extern "C" void app_main()
{
	xTaskCreate(sprinkler_thread_entry, SPRINKLER_TASK_NAME, SPRINKLER_TASK_STACKSIZE, NULL, SPRINKLER_TASK_PRIORITY, NULL);
}
