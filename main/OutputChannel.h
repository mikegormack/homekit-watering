#pragma once
#include <stdint.h>

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
	OutputChannel(const char* name):
		m_name(name)
	{
		evt[0].hour = 8;
		evt[0].min = 0;
		evt[1].hour = 18;
		evt[1].min = 0;
	}

	~OutputChannel()
	{

	}

	void save()
	{

	}

	time_evt_t evt[EVT_PER_CH];
	const char* m_name;
private:
};