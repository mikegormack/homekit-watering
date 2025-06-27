#pragma once

#include <Screen.h>
#include <MenuScreenCtx.h>

class MenuScreen : public Screen
{
public:
	MenuScreen(SSD1306I2C &display, MenuScreenCtx* ctx) :
		Screen(display),
		m_ctx(ctx)
	{
	}

	~MenuScreen()
	{
	}

protected:
	MenuScreenCtx* m_ctx;
};
