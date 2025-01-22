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
#include <SSD1306I2C.h>
#include <esp_log.h>

SSD1306I2C::SSD1306I2C(uint8_t address, i2c_port_t i2c_port, OLEDDISPLAY_GEOMETRY g) :
	_address(address),
	_i2c_port(i2c_port)
{
	setGeometry(g);
}

SSD1306I2C::~SSD1306I2C()
{

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
	RETURN_ERR_CHECK_BOOL(sendCommand(COLUMNADDR, cmd1, sizeof(cmd1)), NULL);
	ESP_LOGI("SSD1306Wire", "Set x %d, %d", x_offset + minBoundX, x_offset + maxBoundX);

	uint8_t cmd2[] = { minBoundY, maxBoundY };
	RETURN_ERR_CHECK_BOOL(sendCommand(PAGEADDR, cmd2, sizeof(cmd2)), NULL);
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
	i2c_cmd_handle_t cmd = i2c_cmd_link_create();
	esp_err_t ret = ESP_OK;
	ret = i2c_master_start(cmd);
	if (ret != ESP_OK)
	{
		ESP_LOGE("SSD1306I2C", "master_start err: %d", ret);
		goto error;
	}
	ret = i2c_master_write_byte(cmd, (this->_address << 1) | I2C_MASTER_WRITE, true);
	if (ret != ESP_OK)
	{
		ESP_LOGE("SSD1306I2C", "master_write_byte addr err: %d", ret);
		goto error;
	}
	ret = i2c_master_write_byte(cmd, OLED_CONTROL_BYTE_DATA_STREAM, true);
	if (ret != ESP_OK)
	{
		ESP_LOGE("SSD1306I2C", "master_write_byte cmd err: %d", ret);
		goto error;
	}
	ret = i2c_master_write(cmd, buf, width, true);
	if (ret != ESP_OK)
	{
		ESP_LOGE("SSD1306I2C", "master_write err: %d", ret);
		goto error;
	}
	ret = i2c_master_stop(cmd);
	if (ret != ESP_OK)
	{
		ESP_LOGE("SSD1306I2C", "master_stop err: %d", ret);
		goto error;
	}
	ret = i2c_master_cmd_begin(I2C_NUM_0, cmd, 50 / portTICK_PERIOD_MS);
	if (ret != ESP_OK)
	{
		ESP_LOGE("SSD1306I2C", "i2c_master_cmd_begin: %d", ret);
		goto error;
	}
	i2c_cmd_link_delete(cmd);
	return true;
error:
	i2c_cmd_link_delete(cmd);
	return false;
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
