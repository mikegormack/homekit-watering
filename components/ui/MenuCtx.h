#pragma once

#include <functional>
#include <memory>

#include <OutputChannels.h>

class MenuCtx
{
public:
	enum class ProvStatus { InProgress, Connected, Failed };

	struct WifiInfo
	{
		bool connected;
		char ssid[33];
		char ip[16];
	};

	using StartProvFn  = std::function<std::unique_ptr<uint8_t[]>()>;
	using StopProvFn   = std::function<void()>;
	using ProvStatusFn = std::function<ProvStatus()>;
	using WifiInfoFn   = std::function<WifiInfo()>;

	MenuCtx(OutputChannels& out_ch) : m_out_ch(out_ch) {}
	~MenuCtx() {}

	OutputChannels& m_out_ch;

	StartProvFn  start_prov;
	StopProvFn   stop_prov;
	ProvStatusFn prov_status;
	WifiInfoFn   wifi_info;
};
