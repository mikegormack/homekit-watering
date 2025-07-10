#pragma once
#include <stdint.h>
#include <nvs_flash.h>
#include <nvs.h>

#define EVT_PER_CH  2

typedef struct
{
	uint8_t hour;
	uint8_t min;
	uint8_t duration;
} time_evt_t;

class OutputChannel
{
public:
	OutputChannel(const char* name);

	~OutputChannel();

	void save();

	void load();

	time_evt_t m_evt[EVT_PER_CH];
	const char* m_name;
private:
};