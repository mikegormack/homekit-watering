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

#include <string>
#include <cstring>
#include <iostream>
#include <sstream>
#include "freertos/FreeRTOS.h"
#include "esp_log.h"

const char *OLEDDisplay::icon_types[] =
{
    "115700000011101000111111110111101111111",
    "115701110100011000011111110111101111111",
    "21127000000000000001110000000010001000000010001111110010001001010001110000010000000000000",
    "11770000000000111000100000100110010100001010100000000",
    "11770000000000000000000000000110000100000010100000000",
    "11770000000000000000000000000000000000000000100000000",
    "11770100010100100110111011001001010101000010000001000",
    "11770001000001110001111101111111011111001101100110110",
    "21127000000000000011110011110100001100001100001100001100001100001011110011110000000000000"
};

OLEDDisplay::~OLEDDisplay()
{
	end();
}

bool OLEDDisplay::init()
{
	if (!this->connect())
	{
		DEBUG_OLEDDISPLAY("[OLEDDISPLAY][init] Can't establish connection to display\n");
		return false;
	}

	if(this->buffer == NULL)
	{
		this->buffer = (uint8_t*) malloc(sizeof(uint8_t) * displayBufferSize);

		if(!this->buffer)
		{
			DEBUG_OLEDDISPLAY("[OLEDDISPLAY][init] Not enough memory to create display\n");
			return false;
		}
	}

	#ifdef OLEDDISPLAY_DOUBLE_BUFFER
	if(this->buffer_back == NULL)
	{
		this->buffer_back = (uint8_t*) malloc(sizeof(uint8_t) * displayBufferSize);

		if(!this->buffer_back)
		{
			DEBUG_OLEDDISPLAY("[OLEDDISPLAY][init] Not enough memory to create back buffer\n");
			free(this->buffer);
			return false;
		}
	}
	#endif

	sendInitCommands();
	resetDisplay();

	return true;
}

void OLEDDisplay::end()
{
	if (this->buffer)
	{
		free(this->buffer);
		this->buffer = NULL;
	}
	#ifdef OLEDDISPLAY_DOUBLE_BUFFER
	if (this->buffer_back)
	{
		free(this->buffer_back);
		this->buffer_back = NULL;
	}
	#endif
	if (this->logBuffer != NULL)
	{
		free(this->logBuffer);
		this->logBuffer = NULL;
	}
}

void OLEDDisplay::resetDisplay(void)
{
	ESP_LOGI("OLEDDisplay", "Reset");
	clear();
	#ifdef OLEDDISPLAY_DOUBLE_BUFFER
	memset(buffer_back, 1, displayBufferSize);
	#endif
	display();
}

void OLEDDisplay::setColor(OLEDDISPLAY_COLOR color)
{
	this->color = color;
}

OLEDDISPLAY_COLOR OLEDDisplay::getColor()
{
	return this->color;
}

void OLEDDisplay::setPixel(int16_t x, int16_t y)
{
	if (x >= 0 && x < this->width() && y >= 0 && y < this->height())
	{
		switch (color)
		{
			case WHITE:
				buffer[x + (y / 8) * this->width()] |=  (1 << (y & 7));
				break;
			case BLACK:
				buffer[x + (y / 8) * this->width()] &= ~(1 << (y & 7));
				break;
			case INVERSE:
				buffer[x + (y / 8) * this->width()] ^=  (1 << (y & 7));
				break;
		}
	}
}

// Bresenham's algorithm - thx wikipedia and Adafruit_GFX
void OLEDDisplay::drawLine(int16_t x0, int16_t y0, int16_t x1, int16_t y1)
{
	int16_t steep = abs(y1 - y0) > abs(x1 - x0);
	if (steep)
	{
		_swap_int16_t(x0, y0);
		_swap_int16_t(x1, y1);
	}

	if (x0 > x1)
	{
		_swap_int16_t(x0, x1);
		_swap_int16_t(y0, y1);
	}

	int16_t dx, dy;
	dx = x1 - x0;
	dy = abs(y1 - y0);

	int16_t err = dx / 2;
	int16_t ystep;

	if (y0 < y1)
	{
		ystep = 1;
	}
	else
	{
		ystep = -1;
	}

	for (; x0 <= x1; x0++)
	{
		if (steep)
		{
			setPixel(y0, x0);
		}
		else
		{
			setPixel(x0, y0);
		}
		err -= dy;
		if (err < 0)
		{
			y0 += ystep;
			err += dx;
		}
	}
}

void OLEDDisplay::drawRect(int16_t x, int16_t y, int16_t width, int16_t height)
{
	drawHorizontalLine(x, y, width);
	drawVerticalLine(x, y, height);
	drawVerticalLine(x + width - 1, y, height);
	drawHorizontalLine(x, y + height - 1, width);
}

void OLEDDisplay::fillRect(int16_t xMove, int16_t yMove, int16_t width, int16_t height)
{
	for (int16_t x = xMove; x < xMove + width; x++)
	{
		drawVerticalLine(x, yMove, height);
	}
}

void OLEDDisplay::drawCircle(int16_t x0, int16_t y0, int16_t radius)
{
	int16_t x = 0, y = radius;
	int16_t dp = 1 - radius;
	do
	{
		if (dp < 0)
			dp = dp + 2 * (++x) + 3;
		else
			dp = dp + 2 * (++x) - 2 * (--y) + 5;

		setPixel(x0 + x, y0 + y);     //For the 8 octants
		setPixel(x0 - x, y0 + y);
		setPixel(x0 + x, y0 - y);
		setPixel(x0 - x, y0 - y);
		setPixel(x0 + y, y0 + x);
		setPixel(x0 - y, y0 + x);
		setPixel(x0 + y, y0 - x);
		setPixel(x0 - y, y0 - x);

	} while (x < y);

	setPixel(x0 + radius, y0);
	setPixel(x0, y0 + radius);
	setPixel(x0 - radius, y0);
	setPixel(x0, y0 - radius);
}

void OLEDDisplay::drawCircleQuads(int16_t x0, int16_t y0, int16_t radius, uint8_t quads)
{
	int16_t x = 0, y = radius;
	int16_t dp = 1 - radius;
	while (x < y)
	{
		if (dp < 0)
			dp = dp + 2 * (++x) + 3;
		else
			dp = dp + 2 * (++x) - 2 * (--y) + 5;
		if (quads & 0x1)
		{
			setPixel(x0 + x, y0 - y);
			setPixel(x0 + y, y0 - x);
		}
		if (quads & 0x2)
		{
			setPixel(x0 - y, y0 - x);
			setPixel(x0 - x, y0 - y);
		}
		if (quads & 0x4)
		{
			setPixel(x0 - y, y0 + x);
			setPixel(x0 - x, y0 + y);
		}
		if (quads & 0x8)
		{
			setPixel(x0 + x, y0 + y);
			setPixel(x0 + y, y0 + x);
		}
	}
	if (quads & 0x1 && quads & 0x8)
	{
		setPixel(x0 + radius, y0);
	}
	if (quads & 0x4 && quads & 0x8)
	{
		setPixel(x0, y0 + radius);
	}
	if (quads & 0x2 && quads & 0x4)
	{
		setPixel(x0 - radius, y0);
	}
	if (quads & 0x1 && quads & 0x2)
	{
		setPixel(x0, y0 - radius);
	}
}


void OLEDDisplay::fillCircle(int16_t x0, int16_t y0, int16_t radius)
{
	int16_t x = 0, y = radius;
	int16_t dp = 1 - radius;
	do
	{
		if (dp < 0)
			dp = dp + 2 * (++x) + 3;
		else
			dp = dp + 2 * (++x) - 2 * (--y) + 5;

		drawHorizontalLine(x0 - x, y0 - y, 2 * x);
		drawHorizontalLine(x0 - x, y0 + y, 2 * x);
		drawHorizontalLine(x0 - y, y0 - x, 2 * y);
		drawHorizontalLine(x0 - y, y0 + x, 2 * y);


	} while (x < y);
	drawHorizontalLine(x0 - radius, y0, 2 * radius);

}

void OLEDDisplay::drawHorizontalLine(int16_t x, int16_t y, int16_t length)
{
	if (y < 0 || y >= this->height())
	{
		return;
	}

	if (x < 0)
	{
		length += x;
		x = 0;
	}

	if ( (x + length) > this->width())
	{
		length = (this->width() - x);
	}

	if (length <= 0)
	{
		return;
	}

	uint8_t * bufferPtr = buffer;
	bufferPtr += (y >> 3) * this->width();
	bufferPtr += x;

	uint8_t drawBit = 1 << (y & 7);

	switch (color)
	{
		case WHITE:
			while (length--)
			{
				*bufferPtr++ |= drawBit;
			};
			break;
		case BLACK:
			drawBit = ~drawBit;
			while (length--)
			{
				*bufferPtr++ &= drawBit;
			};
			break;
		case INVERSE:
			while (length--)
			{
				*bufferPtr++ ^= drawBit;
			};
			break;
	}
}

void OLEDDisplay::drawVerticalLine(int16_t x, int16_t y, int16_t length)
{
	if (x < 0 || x >= this->width()) return;

	if (y < 0)
	{
		length += y;
		y = 0;
	}

	if ( (y + length) > this->height())
	{
		length = (this->height() - y);
	}

	if (length <= 0) return;


	uint8_t yOffset = y & 7;
	uint8_t drawBit;
	uint8_t *bufferPtr = buffer;

	bufferPtr += (y >> 3) * this->width();
	bufferPtr += x;

	if (yOffset)
	{
		yOffset = 8 - yOffset;
		drawBit = ~(0xFF >> (yOffset));

		if (length < yOffset)
		{
			drawBit &= (0xFF >> (yOffset - length));
		}

		switch (color)
		{
			case WHITE:
				*bufferPtr |=  drawBit;
				break;
			case BLACK:
				*bufferPtr &= ~drawBit;
				break;
			case INVERSE:
				*bufferPtr ^=  drawBit;
				break;
		}

		if (length < yOffset) return;

		length -= yOffset;
		bufferPtr += this->width();
	}

	if (length >= 8)
	{
		switch (color)
		{
			case WHITE:
			case BLACK:
				drawBit = (color == WHITE) ? 0xFF : 0x00;
				do
				{
					*bufferPtr = drawBit;
					bufferPtr += this->width();
					length -= 8;
				} while (length >= 8);
				break;
			case INVERSE:
				do
				{
					*bufferPtr = ~(*bufferPtr);
					bufferPtr += this->width();
					length -= 8;
				} while (length >= 8);
				break;
		}
	}

	if (length > 0)
	{
		drawBit = (1 << (length & 7)) - 1;
		switch (color)
		{
			case WHITE:
				*bufferPtr |=  drawBit;
				break;
			case BLACK:
				*bufferPtr &= ~drawBit;
				break;
			case INVERSE:
				*bufferPtr ^=  drawBit;
				break;
		}
	}
}

void OLEDDisplay::drawProgressBar(uint16_t x, uint16_t y, uint16_t width, uint16_t height, uint8_t progress)
{
	uint16_t radius = height / 2;
	uint16_t xRadius = x + radius;
	uint16_t yRadius = y + radius;
	uint16_t doubleRadius = 2 * radius;
	uint16_t innerRadius = radius - 2;

	setColor(WHITE);
	drawCircleQuads(xRadius, yRadius, radius, 0b00000110);
	drawHorizontalLine(xRadius, y, width - doubleRadius + 1);
	drawHorizontalLine(xRadius, y + height, width - doubleRadius + 1);
	drawCircleQuads(x + width - radius, yRadius, radius, 0b00001001);

	uint16_t maxProgressWidth = (width - doubleRadius + 1) * progress / 100;

	fillCircle(xRadius, yRadius, innerRadius);
	fillRect(xRadius + 1, y + 2, maxProgressWidth, height - 3);
	fillCircle(xRadius + maxProgressWidth, yRadius, innerRadius);
}

void OLEDDisplay::drawFastImage(int16_t xMove, int16_t yMove, int16_t width, int16_t height, const uint8_t *image)
{
	drawInternal(xMove, yMove, width, height, image, 0, 0);
}

void OLEDDisplay::drawXbm(int16_t xMove, int16_t yMove, int16_t width, int16_t height, const uint8_t *xbm)
{
	int16_t widthInXbm = (width + 7) / 8;
	uint8_t data = 0;

	for(int16_t y = 0; y < height; y++)
	{
		for(int16_t x = 0; x < width; x++ )
		{
			if (x & 7)
			{
				data >>= 1; // Move a bit
			}
			else      // Read new data every 8 bit
			{
				data = pgm_read_byte(xbm + (x / 8) + y * widthInXbm);
			}
			// if there is a bit draw it
			if (data & 0x01)
			{
				setPixel(xMove + x, yMove + y);
			}
		}
	}
}

int OLEDDisplay::icons_StringToNumber(const std::string &Text)
{
    std::istringstream ss(Text);
	int result;
	return ss >> result ? result : 0;
}


void OLEDDisplay::drawIcon(Icon icon, int xpos, int ypos)
{
	std::string iconValue = icon_types[icon];
	int sizeDigetsx = icons_StringToNumber(iconValue.substr(0,1));
	int sizeDigetsy = icons_StringToNumber(iconValue.substr(1,1));
	int width = icons_StringToNumber(iconValue.substr(2,sizeDigetsx));
	int height = icons_StringToNumber(iconValue.substr(sizeDigetsx+2,sizeDigetsy));

	for(int y = 0; y < height; ++y)
	{
		std::string line = iconValue.substr(((sizeDigetsx+sizeDigetsy+2)+(y*width)), width);

		for (int x = 0; x < width; ++x)
		{
			int populated = icons_StringToNumber(line.substr(x, 1));
			//cout << icons_StringToNumber<int>(line.substr(x, 1));
			if (populated == 1)
				setPixel(xpos + x, ypos + y);

		}
	}
	//cout << '\n';
}

void OLEDDisplay::drawBitmap(int16_t x, int16_t y, const uint8_t* bitmap, int16_t w, int16_t h)
{
	int16_t byteWidth = (w + 7) / 8;
	uint8_t b = 0;
	for (int16_t j = 0; j < h; j++, y++)
	{
		for (int16_t i = 0; i < w; i++)
		{
			if (i & 7)
				b <<= 1;
            else
                b = bitmap[j * byteWidth + i / 8];
            if (b & 0x80)
                setPixel(x + i, y);
	    }
	}
}

void OLEDDisplay::drawStringInternal(int16_t xMove, int16_t yMove, char* text, uint16_t textLength, uint16_t textWidth)
{
	uint8_t textHeight       = pgm_read_byte(fontData + HEIGHT_POS);
	uint8_t firstChar        = pgm_read_byte(fontData + FIRST_CHAR_POS);
	uint16_t sizeOfJumpTable = pgm_read_byte(fontData + CHAR_NUM_POS)  * JUMPTABLE_BYTES;

	uint8_t cursorX         = 0;
	uint8_t cursorY         = 0;

	//ESP_LOGI("OLEDDisplay", "drawStringInternal = %d", textLength);

	switch (textAlignment)
	{
		case TEXT_ALIGN_CENTER_BOTH:
			yMove -= textHeight >> 1;
		// Fallthrough
		case TEXT_ALIGN_CENTER:
			xMove -= textWidth >> 1; // divide by 2
			break;
		case TEXT_ALIGN_RIGHT:
			xMove -= textWidth;
			break;
		case TEXT_ALIGN_LEFT:
			break;
	}

	// Don't draw anything if it is not on the screen.
	if (xMove + textWidth  < 0 || xMove > this->width() )
	{
		return;
	}
	if (yMove + textHeight < 0 || yMove > this->width() )
	{
		return;
	}

	for (uint16_t j = 0; j < textLength; j++)
	{
		int16_t xPos = xMove + cursorX;
		int16_t yPos = yMove + cursorY;

		uint8_t code = text[j];
		if (code >= firstChar)
		{
			uint8_t charCode = code - firstChar;

			// 4 uint8_ts per char code
			uint8_t msbJumpToChar    = pgm_read_byte( fontData + JUMPTABLE_START + charCode * JUMPTABLE_BYTES );                  // MSB  \ JumpAddress
			uint8_t lsbJumpToChar    = pgm_read_byte( fontData + JUMPTABLE_START + charCode * JUMPTABLE_BYTES + JUMPTABLE_LSB);   // LSB /
			uint8_t charByteSize     = pgm_read_byte( fontData + JUMPTABLE_START + charCode * JUMPTABLE_BYTES + JUMPTABLE_SIZE);  // Size
			uint8_t currentCharWidth = pgm_read_byte( fontData + JUMPTABLE_START + charCode * JUMPTABLE_BYTES + JUMPTABLE_WIDTH); // Width

			// Test if the char is drawable
			if (!(msbJumpToChar == 255 && lsbJumpToChar == 255))
			{
				// Get the position of the char data
				uint16_t charDataPosition = JUMPTABLE_START + sizeOfJumpTable + ((msbJumpToChar << 8) + lsbJumpToChar);
				drawInternal(xPos, yPos, currentCharWidth, textHeight, fontData, charDataPosition, charByteSize);
			}

			cursorX += currentCharWidth;
		}
	}
}


void OLEDDisplay::drawString(int16_t xMove, int16_t yMove, std::string strUser)
{
	uint16_t lineHeight = pgm_read_byte(fontData + HEIGHT_POS);
	// char* text must be freed!
	char* text = utf8ascii(strUser);

	uint16_t yOffset = 0;
	// If the string should be centered vertically too
	// we need to now how heigh the string is.
	if (textAlignment == TEXT_ALIGN_CENTER_BOTH)
	{
		uint16_t lb = 0;
		// Find number of linebreaks in text
		for (uint16_t i = 0; text[i] != 0; i++)
		{
			lb += (text[i] == 10);
		}
		// Calculate center
		yOffset = (lb * lineHeight) / 2;
	}

	uint16_t line = 0;
	char* textPart = strtok(text, "\n");
	while (textPart != NULL)
	{
		uint16_t length = strlen(textPart);
		drawStringInternal(xMove, yMove - yOffset + (line++) * lineHeight, textPart, length, getStringWidth(textPart, length));
		textPart = strtok(NULL, "\n");
	}
	free(text);
}

void OLEDDisplay::drawStringMaxWidth(int16_t xMove, int16_t yMove, uint16_t maxLineWidth, std::string strUser)
{
	uint16_t firstChar  = pgm_read_byte(fontData + FIRST_CHAR_POS);
	uint16_t lineHeight = pgm_read_byte(fontData + HEIGHT_POS);

	char* text = utf8ascii(strUser);

	uint16_t length = strlen(text);
	uint16_t lastDrawnPos = 0;
	uint16_t lineNumber = 0;
	uint16_t strWidth = 0;

	uint16_t preferredBreakpoint = 0;
	uint16_t widthAtBreakpoint = 0;

	for (uint16_t i = 0; i < length; i++)
	{
		strWidth += pgm_read_byte(fontData + JUMPTABLE_START + (text[i] - firstChar) * JUMPTABLE_BYTES + JUMPTABLE_WIDTH);

		// Always try to break on a space or dash
		if (text[i] == ' ' || text[i] == '-')
		{
			preferredBreakpoint = i;
			widthAtBreakpoint = strWidth;
		}

		if (strWidth >= maxLineWidth)
		{
			if (preferredBreakpoint == 0)
			{
				preferredBreakpoint = i;
				widthAtBreakpoint = strWidth;
			}
			drawStringInternal(xMove, yMove + (lineNumber++) * lineHeight, &text[lastDrawnPos], preferredBreakpoint - lastDrawnPos, widthAtBreakpoint);
			lastDrawnPos = preferredBreakpoint + 1;
			// It is possible that we did not draw all letters to i so we need
			// to account for the width of the chars from `i - preferredBreakpoint`
			// by calculating the width we did not draw yet.
			strWidth = strWidth - widthAtBreakpoint;
			preferredBreakpoint = 0;
		}
	}

	// Draw last part if needed
	if (lastDrawnPos < length)
	{
		drawStringInternal(xMove, yMove + lineNumber * lineHeight, &text[lastDrawnPos], length - lastDrawnPos, getStringWidth(&text[lastDrawnPos], length - lastDrawnPos));
	}

	free(text);
}

uint16_t OLEDDisplay::getStringWidth(const char* text, uint16_t length)
{
	uint16_t firstChar        = pgm_read_byte(fontData + FIRST_CHAR_POS);

	uint16_t stringWidth = 0;
	uint16_t maxWidth = 0;

	while (length--)
	{
		stringWidth += pgm_read_byte(fontData + JUMPTABLE_START + (text[length] - firstChar) * JUMPTABLE_BYTES + JUMPTABLE_WIDTH);
		if (text[length] == 10)
		{
			maxWidth = std::max(maxWidth, stringWidth);
			stringWidth = 0;
		}
	}

	return std::max(maxWidth, stringWidth);
}

uint16_t OLEDDisplay::getStringWidth(std::string strUser)
{
	char* text = utf8ascii(strUser);
	uint16_t length = strlen(text);
	uint16_t width = getStringWidth(text, length);
	free(text);
	return width;
}

void OLEDDisplay::setTextAlignment(OLEDDISPLAY_TEXT_ALIGNMENT textAlignment)
{
	this->textAlignment = textAlignment;
}

void OLEDDisplay::setFont(const uint8_t *fontData)
{
	this->fontData = fontData;
}

void OLEDDisplay::displayOn(void)
{
	sendCommand(DISPLAYON);
}

void OLEDDisplay::displayOff(void)
{
	sendCommand(DISPLAYOFF);
}

void OLEDDisplay::invertDisplay(void)
{
	sendCommand(INVERTDISPLAY);
}

void OLEDDisplay::normalDisplay(void)
{
	sendCommand(NORMALDISPLAY);
}

void OLEDDisplay::setContrast(uint8_t contrast, uint8_t precharge, uint8_t comdetect)
{
	uint8_t cmd1[1];
	cmd1[0] = precharge; //0xF1 default, to lower the contrast, put 1-1F
	sendCommand(SETPRECHARGE, cmd1, sizeof(cmd1));
	cmd1[0] = contrast; // 0-255
	sendCommand(SETCONTRAST, cmd1, sizeof(cmd1));
	cmd1[0] = comdetect; //0x40 default, to lower the contrast, put 0
	sendCommand(SETVCOMDETECT, cmd1, sizeof(cmd1));
	sendCommand(DISPLAYALLON_RESUME);
	sendCommand(NORMALDISPLAY);
	sendCommand(DISPLAYON);
}

void OLEDDisplay::setBrightness(uint8_t brightness)
{
	uint8_t contrast = brightness;
	if (brightness < 128)
	{
		// Magic values to get a smooth/ step-free transition
		contrast = brightness * 1.171;
	}
	else
	{
		contrast = brightness * 1.171 - 43;
	}

	uint8_t precharge = 241;
	if (brightness == 0)
	{
		precharge = 0;
	}
	uint8_t comdetect = brightness / 8;

	setContrast(contrast, precharge, comdetect);
}

void OLEDDisplay::resetOrientation()
{
	sendCommand(SEGREMAP);
	sendCommand(COMSCANINC);           //Reset screen rotation or mirroring
}

void OLEDDisplay::flipScreenVertically()
{
	sendCommand(SEGREMAP | 0x01);
	sendCommand(COMSCANDEC);           //Rotate screen 180 Deg
}

void OLEDDisplay::mirrorScreen()
{
	sendCommand(SEGREMAP);
	sendCommand(COMSCANDEC);           //Mirror screen
}

void OLEDDisplay::clear(void)
{
	memset(buffer, 0, displayBufferSize);
}

void OLEDDisplay::drawLogBuffer(uint16_t xMove, uint16_t yMove)
{
	uint16_t lineHeight = pgm_read_byte(fontData + HEIGHT_POS);
	// Always align left
	setTextAlignment(TEXT_ALIGN_LEFT);

	// State values
	uint16_t length   = 0;
	uint16_t line     = 0;
	uint16_t lastPos  = 0;

	for (uint16_t i = 0; i < this->logBufferFilled; i++)
	{
		// Everytime we have a \n print
		if (this->logBuffer[i] == 10)
		{
			length++;
			// Draw string on line `line` from lastPos to length
			// Passing 0 as the lenght because we are in TEXT_ALIGN_LEFT
			drawStringInternal(xMove, yMove + (line++) * lineHeight, &this->logBuffer[lastPos], length, 0);
			// Remember last pos
			lastPos = i;
			// Reset length
			length = 0;
		}
		else
		{
			// Count chars until next linebreak
			length++;
		}
	}
	// Draw the remaining string
	if (length > 0)
	{
		drawStringInternal(xMove, yMove + line * lineHeight, &this->logBuffer[lastPos], length, 0);
	}
}

uint16_t OLEDDisplay::getWidth(void)
{
	return displayWidth;
}

uint16_t OLEDDisplay::getHeight(void)
{
	return displayHeight;
}

bool OLEDDisplay::setLogBuffer(uint16_t lines, uint16_t chars)
{
	if (logBuffer != NULL) free(logBuffer);
	uint16_t size = lines * chars;
	if (size > 0)
	{
		this->logBufferLine     = 0;      // Lines printed
		this->logBufferFilled   = 0;      // Nothing stored yet
		this->logBufferMaxLines = lines;  // Lines max printable
		this->logBufferSize     = size;   // Total number of characters the buffer can hold
		this->logBuffer         = (char *) malloc(size * sizeof(uint8_t));
		if(!this->logBuffer)
		{
			DEBUG_OLEDDISPLAY("[OLEDDISPLAY][setLogBuffer] Not enough memory to create log buffer\n");
			return false;
		}
	}
	return true;
}

size_t OLEDDisplay::write(uint8_t c)
{
	if (this->logBufferSize > 0)
	{
		// Don't waste space on \r\n line endings, dropping \r
		if (c == 13) return 1;

		// convert UTF-8 character to font table index
		c = (this->fontTableLookupFunction)(c);
		// drop unknown character
		if (c == 0) return 1;

		bool maxLineNotReached = this->logBufferLine < this->logBufferMaxLines;
		bool bufferNotFull = this->logBufferFilled < this->logBufferSize;

		// Can we write to the buffer?
		if (bufferNotFull && maxLineNotReached)
		{
			this->logBuffer[logBufferFilled] = c;
			this->logBufferFilled++;
			// Keep track of lines written
			if (c == 10) this->logBufferLine++;
		}
		else
		{
			// Max line number is reached
			if (!maxLineNotReached) this->logBufferLine--;

			// Find the end of the first line
			uint16_t firstLineEnd = 0;
			for (uint16_t i = 0; i < this->logBufferFilled; i++)
			{
				if (this->logBuffer[i] == 10)
				{
					// Include last char too
					firstLineEnd = i + 1;
					break;
				}
			}
			// If there was a line ending
			if (firstLineEnd > 0)
			{
				// Calculate the new logBufferFilled value
				this->logBufferFilled = logBufferFilled - firstLineEnd;
				// Now we move the lines infront of the buffer
				memcpy(this->logBuffer, &this->logBuffer[firstLineEnd], logBufferFilled);
			}
			else
			{
				// Let's reuse the buffer if it was full
				if (!bufferNotFull)
				{
					this->logBufferFilled = 0;
				}// else {
				//  Nothing to do here
				//}
			}
			write(c);
		}
	}
	// We are always writing all uint8_t to the buffer
	return 1;
}

size_t OLEDDisplay::write(const char* str)
{
	if (str == NULL) return 0;
	size_t length = strlen(str);
	for (size_t i = 0; i < length; i++)
	{
		write(str[i]);
	}
	return length;
}

// Private functions
void OLEDDisplay::setGeometry(OLEDDISPLAY_GEOMETRY g)
{
	this->geometry = g;
	if (g == GEOMETRY_128_64)
	{
		this->displayWidth                     = 128;
		this->displayHeight                    = 64;
	}
	else if (g == GEOMETRY_128_32)
	{
		this->displayWidth                     = 128;
		this->displayHeight                    = 32;
	}
	this->displayBufferSize                = displayWidth * displayHeight / 8;
}

void OLEDDisplay::sendInitCommands(void)
{
	uint8_t cmd1[1];
	sendCommand(DISPLAYOFF);
	cmd1[0] = 0xF0;
	sendCommand(SETDISPLAYCLOCKDIV, cmd1, sizeof(cmd1));
	sendCommand(SETMULTIPLEX);
	sendCommand(this->height() - 1);
	cmd1[0] = 0x00;
	sendCommand(SETDISPLAYOFFSET, cmd1, sizeof(cmd1));
	sendCommand(SETSTARTLINE);
	cmd1[0] = 0x14;
	sendCommand(CHARGEPUMP, cmd1, sizeof(cmd1));
	cmd1[0] = 0x00;
	sendCommand(MEMORYMODE, cmd1, sizeof(cmd1));
	sendCommand(SEGREMAP);
	sendCommand(COMSCANINC);
	if (geometry == GEOMETRY_128_64)
	{
		cmd1[0] = 0x12;
	}
	else if (geometry == GEOMETRY_128_32)
	{
		cmd1[0] = 0x02;
	}
	sendCommand(SETCOMPINS, cmd1, sizeof(cmd1));
	if (geometry == GEOMETRY_128_64)
	{
		cmd1[0] = 0xCF;
	}
	else if (geometry == GEOMETRY_128_32)
	{
		cmd1[0] = 0x8F;
	}
	sendCommand(SETCONTRAST, cmd1, sizeof(cmd1));
	cmd1[0] = 0xF1;
	sendCommand(SETPRECHARGE, cmd1, sizeof(cmd1));
	sendCommand(SETVCOMDETECT); //0xDB, (additionally needed to lower the contrast)
	cmd1[0] = 0x40;            //0x40 default, to lower the contrast, put 0
	sendCommand(SETVCOMDETECT, cmd1, sizeof(cmd1));
	sendCommand(DISPLAYALLON_RESUME);
	cmd1[0] = 0x2e;            // stop scroll
	sendCommand(NORMALDISPLAY, cmd1, sizeof(cmd1));
	sendCommand(DISPLAYON);
}

void inline OLEDDisplay::drawInternal(int16_t xMove, int16_t yMove, int16_t width, int16_t height, const uint8_t *data, uint16_t offset, uint16_t bytesInData)
{
	if (width < 0 || height < 0) return;
	if (yMove + height < 0 || yMove > this->height())  return;
	if (xMove + width  < 0 || xMove > this->width())   return;

	//ESP_LOGI("OLEDDisplay", "drawInternal = %d", bytesInData);

	uint8_t  rasterHeight = 1 + ((height - 1) >> 3); // fast ceil(height / 8.0)
	int8_t   yOffset      = yMove & 7;

	bytesInData = bytesInData == 0 ? width * rasterHeight : bytesInData;

	int16_t initYMove   = yMove;
	int8_t  initYOffset = yOffset;


	for (uint16_t i = 0; i < bytesInData; i++)
	{
		// Reset if next horizontal drawing phase is started.
		if ( i % rasterHeight == 0)
		{
			yMove   = initYMove;
			yOffset = initYOffset;
		}

		uint8_t currentByte = pgm_read_byte(data + offset + i);

		int16_t xPos = xMove + (i / rasterHeight);
		int16_t yPos = ((yMove >> 3) + (i % rasterHeight)) * this->width();

		int16_t dataPos    = xPos  + yPos;

		if (dataPos >=  0  && dataPos < displayBufferSize &&
		        xPos    >=  0  && xPos    < this->width() )
		{

			if (yOffset >= 0)
			{
				switch (this->color)
				{
					case WHITE:
						buffer[dataPos] |= currentByte << yOffset;
						break;
					case BLACK:
						buffer[dataPos] &= ~(currentByte << yOffset);
						break;
					case INVERSE:
						buffer[dataPos] ^= currentByte << yOffset;
						break;
				}

				if (dataPos < (displayBufferSize - this->width()))
				{
					switch (this->color)
					{
						case WHITE:
							buffer[dataPos + this->width()] |= currentByte >> (8 - yOffset);
							break;
						case BLACK:
							buffer[dataPos + this->width()] &= ~(currentByte >> (8 - yOffset));
							break;
						case INVERSE:
							buffer[dataPos + this->width()] ^= currentByte >> (8 - yOffset);
							break;
					}
				}
			}
			else
			{
				// Make new offset position
				yOffset = -yOffset;

				switch (this->color)
				{
					case WHITE:
						buffer[dataPos] |= currentByte >> yOffset;
						break;
					case BLACK:
						buffer[dataPos] &= ~(currentByte >> yOffset);
						break;
					case INVERSE:
						buffer[dataPos] ^= currentByte >> yOffset;
						break;
				}

				// Prepare for next iteration by moving one block up
				yMove -= 8;

				// and setting the new yOffset
				yOffset = 8 - yOffset;
			}

			//yield();
		}
	}
}

// You need to free the char!
char* OLEDDisplay::utf8ascii(std::string str)
{
	uint16_t k = 0;
	uint16_t length = str.length() + 1;

	// Copy the string into a char array
	char* s = (char*) malloc(length * sizeof(char));
	if(!s)
	{
		DEBUG_OLEDDISPLAY("[OLEDDISPLAY][utf8ascii] Can't allocate another char array. Drop support for UTF-8.\n");
		return (char*) str.c_str();
	}
	length--;
	for (uint16_t i = 0; i < length; i++)
	{
		s[i] = str[i];
		char c = (this->fontTableLookupFunction)(s[i]);
		if (c != 0)
		{
			s[k++] = c;
		}
	}
	s[k] = 0;
	// This will leak 's' be sure to free it in the calling function.
	return s;
}

void OLEDDisplay::setFontTableLookupFunction(FontTableLookupFunction function)
{
	this->fontTableLookupFunction = function;
}

uint8_t OLEDDisplay::pgm_read_byte(const uint8_t *pData)
{
	return *pData;
}
