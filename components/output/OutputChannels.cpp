
#include <OutputChannels.h>

OutputChannels::OutputChannels() :
	m_channels { OutputChannel("CH1"), OutputChannel("CH2") }
{
}

OutputChannels::~OutputChannels()
{
}

void OutputChannels::load()
{
	for (uint8_t i = 0; i < NUM_CH; i++)
	{
		m_channels[i].load();
	}
}


OutputChannel* OutputChannels::getChannel(ch_id id)
{
	if (id < NUM_CH)
		return &m_channels[id];
	else
		return nullptr;
}
