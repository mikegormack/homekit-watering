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

#include <driver/i2c.h>
#include "SSD1306I2C.h"
#include "esp_log.h"

SSD1306I2C::SSD1306I2C(uint8_t _address, uint8_t _sda, uint8_t _scl, OLEDDISPLAY_GEOMETRY g)
{
	setGeometry(g);

	this->_address = _address;
	this->_sda = _sda;
	this->_scl = _scl;
}

SSD1306I2C::~SSD1306I2C()
{

}

bool SSD1306I2C::connect()
{
	esp_err_t ret = ESP_OK;
	i2c_config_t i2c_config =
	{
		.mode = I2C_MODE_MASTER,
		.sda_io_num = this->_sda,
		.scl_io_num = this->_scl,
		.sda_pullup_en = GPIO_PULLUP_ENABLE,
		.scl_pullup_en = GPIO_PULLUP_ENABLE,
		.master = {.clk_speed = 400000},
		.clk_flags = 0
	};
	ret = i2c_param_config(I2C_NUM_0, &i2c_config);
	if (ret != ESP_OK) return false;
	ret = i2c_driver_install(I2C_NUM_0, I2C_MODE_MASTER, 0, 0, 0);
	if (ret != ESP_OK) return false;
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

	uint8_t cmd1[] = { (uint8_t)(x_offset + minBoundX), (uint8_t)(x_offset + maxBoundX) };
	RETURN_ERR_CHECK_BOOL(sendCommand(COLUMNADDR, cmd1, sizeof(cmd1)), NULL);
	//ESP_LOGI("SSD1306Wire", "Set x %d, %d", x_offset + minBoundX, x_offset + maxBoundX);

	uint8_t cmd2[] = { minBoundY, maxBoundY };
	RETURN_ERR_CHECK_BOOL(sendCommand(PAGEADDR, cmd2, sizeof(cmd2)), NULL);
	//ESP_LOGI("SSD1306Wire", "Set y %d, %d", minBoundY, maxBoundY);

	i2c_cmd_handle_t cmd = i2c_cmd_link_create();
	RETURN_ERR_CHECK_BOOL(i2c_master_start(cmd) == ESP_OK, cmd);
	RETURN_ERR_CHECK_BOOL(i2c_master_write_byte(cmd, (this->_address << 1) | I2C_MASTER_WRITE, true) == ESP_OK, cmd);
	RETURN_ERR_CHECK_BOOL(i2c_master_write_byte(cmd, OLED_CONTROL_BYTE_DATA_STREAM, true) == ESP_OK, cmd);

	for (y = minBoundY; y <= maxBoundY; y++)
	{
		for (x = minBoundX; x <= maxBoundX; x++)
		{
			RETURN_ERR_CHECK_BOOL(i2c_master_write_byte(cmd, buffer[x + y * this->width()], true) == ESP_OK, cmd);
		}
	}

	RETURN_ERR_CHECK_BOOL(i2c_master_stop(cmd) == ESP_OK, cmd);
	RETURN_ERR_CHECK_BOOL(i2c_master_cmd_begin(I2C_NUM_0, cmd, 10 / portTICK_PERIOD_MS) == ESP_OK, cmd);

	i2c_cmd_link_delete(cmd);
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
	i2c_cmd_handle_t cmd = i2c_cmd_link_create();
	RETURN_ERR_CHECK_BOOL(i2c_master_start(cmd) == ESP_OK, cmd);
	RETURN_ERR_CHECK_BOOL(i2c_master_write_byte(cmd, (this->_address << 1) | I2C_MASTER_WRITE, true) == ESP_OK, cmd);
	RETURN_ERR_CHECK_BOOL(i2c_master_write_byte(cmd, OLED_CONTROL_BYTE_CMD_STREAM, true) == ESP_OK, cmd);
	RETURN_ERR_CHECK_BOOL(i2c_master_write_byte(cmd, command, true) == ESP_OK, cmd);
	while (len-- > 0)
	{
		RETURN_ERR_CHECK_BOOL(i2c_master_write_byte(cmd, *pDat, true) == ESP_OK, cmd);
		pDat++;
	}
	RETURN_ERR_CHECK_BOOL(i2c_master_stop(cmd) == ESP_OK, cmd);
	esp_err_t err = i2c_master_cmd_begin(I2C_NUM_0, cmd, 10 / portTICK_PERIOD_MS);
	i2c_cmd_link_delete(cmd);
	//ESP_LOGI("SSD1306Wire", "Sent Command %d result %d", command, err);
	return (err == ESP_OK);
}