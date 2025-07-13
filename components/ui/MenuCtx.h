#pragma once

#include <OutputChannels.h>

class MenuCtx
{
public:
	MenuCtx(OutputChannels& out_ch) :
		m_out_ch(out_ch)
	{
	}

	~MenuCtx()
	{
	}

	OutputChannels& m_out_ch;
};
