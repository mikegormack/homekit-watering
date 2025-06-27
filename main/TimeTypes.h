#pragma once

#include <SSD1306I2C.h>
#include <icons.h>

#define EVT_PER_CH  2

typedef struct
{
	uint8_t hour;
	uint8_t min;
	uint8_t duration;
} time_evt_t;

typedef struct
{
	time_evt_t evt[EVT_PER_CH];
} ch_time_t;
