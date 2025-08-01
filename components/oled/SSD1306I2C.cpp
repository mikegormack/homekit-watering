/**
 * The MIT License (MIT)
 *
 * Copyright (c) 2018 by ThingPulse, Daniel Eichhorn
 * Copyright (c) 2018 by Fabrice Weinberg
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 *
 * ThingPulse invests considerable time and money to develop these open source libraries.
 * Please support us by buying our products (and not the clones) from
 * https://thingpulse.com
 *
 */

#include "OLEDDisplay.h"

#include <driver/i2c_master.h>
#include <SSD1306I2C.h>
#include <esp_log.h>

#include <freertos/freeRTOS.h>

#include <vector>
#include <cstring>

#define I2C_FREQ                400000

static const char *TAG = "SSD1306I2C";

SSD1306I2C::SSD1306I2C(uint8_t address, i2c_master_bus_handle_t i2c_bus, OLEDDISPLAY_GEOMETRY g)
{
	i2c_device_config_t i2c_dev_conf =
	{
		.dev_addr_length = I2C_ADDR_BIT_LEN_7,
		.device_address = address,
		.scl_speed_hz = I2C_FREQ,
		.scl_wait_us = 0,
		.flags =
		{
			.disable_ack_check = 0
		}
	};

	esp_err_t ret = i2c_master_bus_add_device(i2c_bus, &i2c_dev_conf, &_i2c_handle);
	if (ret != ESP_OK)
	{
		ESP_LOGE(TAG, "Failed to add i2c device %d", ret);
	}
	else
	{
		ESP_LOGI(TAG, "Added i2c device");
	}
	setGeometry(g);
}

SSD1306I2C::~SSD1306I2C()
{
	esp_err_t ret = i2c_master_bus_rm_device(_i2c_handle);
	if (ret != ESP_OK)
	{
		ESP_LOGE(TAG, "Failed to remove i2c device %d", ret);
	}
}

bool SSD1306I2C::connect()
{
	return true;
}

bool SSD1306I2C::display(void)
{
	//ESP_LOGI("SSD1306Wire", "Display");
	uint8_t x_offset = (128 - this->width()) / 2;
#ifdef OLEDDISPLAY_DOUBLE_BUFFER

	uint8_t minBoundY = UINT8_MAX;
	uint8_t maxBoundY = 0;

	uint8_t minBoundX = UINT8_MAX;
	uint8_t maxBoundX = 0;
	uint8_t x, y;

	// Calculate the Y bounding box of changes
	// and copy buffer[pos] to buffer_back[pos];
	for (y = 0; y < (this->height() / 8); y++)
	{
		for (x = 0; x < this->width(); x++)
		{
			uint16_t pos = x + y * this->width();
			if (buffer[pos] != buffer_back[pos])
			{
				minBoundY = std::min(minBoundY, y);
				maxBoundY = std::max(maxBoundY, y);
				minBoundX = std::min(minBoundX, x);
				maxBoundX = std::max(maxBoundX, x);
			}
			buffer_back[pos] = buffer[pos];
		}
	}

	// If the minBoundY wasn't updated
	// we can savely assume that buffer_back[pos] == buffer[pos]
	// holdes true for all values of pos
	if (minBoundY == UINT8_MAX) return true;

	// set addressing bounds (horizontal mode)
	uint8_t cmd1[] = { (uint8_t)(x_offset + minBoundX), (uint8_t)(x_offset + maxBoundX) };
	/*RETURN_ERR_CHECK_BOOL(*/sendCommand(COLUMNADDR, cmd1, sizeof(cmd1));//, NULL);
	ESP_LOGI("SSD1306Wire", "Set x %d, %d", x_offset + minBoundX, x_offset + maxBoundX);

	uint8_t cmd2[] = { minBoundY, maxBoundY };
	/*RETURN_ERR_CHECK_BOOL(*/sendCommand(PAGEADDR, cmd2, sizeof(cmd2));//, NULL);
	ESP_LOGI("SSD1306Wire", "Set y %d, %d", minBoundY, maxBoundY);

	// write each page up to specified width (horizontal addressing mode)
	int page = minBoundY;
	int width = maxBoundX - minBoundX + 1;

	while (page <= maxBoundY)
	{
		// calc the index into the buffer
		int index = (page * this->width()) + minBoundX;

		ESP_LOGI("SSD1306I2C", "Write to page %d width %d index %d", page, width, index);
		if (sendPageData(&buffer[index], width) == false)
		{
			return false;
		}
		ESP_LOGI("SSD1306I2C", "Write ok");
		page++;
	}

#else

	uint8_t cmd1[] = { x_offset, x_offset + (this->width() - 1) };
	RETURN_ERR_CHECK_BOOL(sendCommand(COLUMNADDR, cmd1, sizeof(cmd1)));

	uint8_t cmd2[] = { 0, (this->height() / 8) - 1};
	RETURN_ERR_CHECK_BOOL(sendCommand(PAGEADDR, cmd2, sizeof(cmd2)));

	if (geometry == GEOMETRY_128_64)
	{
		sendCommand(0x7);
	}
	else if (geometry == GEOMETRY_128_32)
	{
		sendCommand(0x3);
	}

	for (uint16_t i = 0; i < displayBufferSize; i++)
	{
		Wire.beginTransmission(this->_address);
		Wire.write(0x40);
		for (uint8_t x = 0; x < 16; x++)
		{
			Wire.write(buffer[i]);
			i++;
		}
		i--;
		Wire.endTransmission();
	}
#endif
	return true;
}

bool SSD1306I2C::sendPageData(const uint8_t* buf, size_t width)
{
	std::vector<uint8_t> data(width + 1);
	data[0] = OLED_CONTROL_BYTE_DATA_STREAM;
	std::memcpy(data.data() + 1, buf, width);

	esp_err_t ret = i2c_master_transmit(_i2c_handle, data.data(), data.size(), 500);
	if (ret != ESP_OK)
	{
		ESP_LOGE(TAG, "Page data error %d", ret);
		return false;
	}
	return true;
}

uint8_t SSD1306I2C::max(uint8_t a, uint8_t b)
{
	if (a > b) return a;
	return b;
}

bool SSD1306I2C::sendCommand(uint8_t command)
{
	return sendCommand(command, NULL, 0);
}

bool SSD1306I2C::sendCommand(uint8_t command, uint8_t *pDat, uint8_t len)
{
	std::vector<uint8_t> data(len + 2);
	data[0] = OLED_CONTROL_BYTE_CMD_STREAM;
	data[1] = command;
	std::memcpy(data.data() + 2, pDat, len);

	esp_err_t ret = i2c_master_transmit(_i2c_handle, data.data(), data.size(), 50);
	if (ret != ESP_OK)
	{
		ESP_LOGE(TAG, "Command error %d", ret);
		return false;
	}
	return true;
}
