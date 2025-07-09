
#include <OutputChannels.h>



OutputChannels::OutputChannels() :
	m_channels { OutputChannel("CH1"), OutputChannel("CH2") }
{
}

OutputChannels::~OutputChannels()
{
}

OutputChannel* OutputChannels::getChannel(ch_id id)
{
	if (id < NUM_CH)
		return &m_channels[id];
	else
		return nullptr;
}
