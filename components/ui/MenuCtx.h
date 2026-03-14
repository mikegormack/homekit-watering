#pragma once

#include <functional>
#include <memory>

#include <OutputChannels.h>

class MenuCtx
{
public:
	using StartProvFn   = std::function<std::unique_ptr<uint8_t[]>()>;
	using StopProvFn    = std::function<void()>;
	using IsConnectedFn = std::function<bool()>;

	MenuCtx(OutputChannels& out_ch) : m_out_ch(out_ch) {}
	~MenuCtx() {}

	OutputChannels& m_out_ch;

	StartProvFn   start_prov;
	StopProvFn    stop_prov;
	IsConnectedFn is_connected;
};
