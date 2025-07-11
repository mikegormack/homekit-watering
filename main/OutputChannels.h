#pragma once

#include <OutputChannel.h>


typedef enum
{
	CH_ID_WATER_1,
	CH_ID_WATER_2,
	NUM_CH          // sentinel val
} ch_id;

class OutputChannels
{
public:
	OutputChannels();
	~OutputChannels();

	void load();

	OutputChannel* getChannel(ch_id id);
private:
	OutputChannel m_channels[NUM_CH];
};
