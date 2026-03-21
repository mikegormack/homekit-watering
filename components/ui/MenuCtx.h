#pragma once

#include <functional>
#include <memory>
#include <string>

#include <OutputChannel.h>

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

	using StartProvFn   = std::function<std::unique_ptr<uint8_t[]>()>;
	using StopProvFn    = std::function<void()>;
	using ProvStatusFn  = std::function<ProvStatus()>;
	using WifiInfoFn    = std::function<WifiInfo()>;
	using RssiGetterFn   = std::function<int8_t()>;
	using MoistGetterFn  = std::function<uint8_t()>;
	using TzGetterFn         = std::function<int()>;
	using TzSetterFn         = std::function<void(int)>;
	using HapIsPairedFn      = std::function<bool()>;
	using HapReopenPairingFn = std::function<void()>;
	using ChActiveFn         = std::function<bool(int ch)>;      // ch = 0 or 1
	using ChRemainingFn      = std::function<uint32_t(int ch)>;  // seconds remaining

	MenuCtx() {}
	~MenuCtx() {}

	// Pointers to the two valve channels (as OutputChannel for schedule editing)
	// Set by app_main before UI is created
	OutputChannel *out_ch[2] = {nullptr, nullptr};

	StartProvFn  start_prov;
	StopProvFn   stop_prov;
	ProvStatusFn prov_status;
	WifiInfoFn   wifi_info;
	RssiGetterFn  rssi_getter;
	MoistGetterFn moist_getter;
	TzGetterFn         tz_getter;
	TzSetterFn         tz_setter;
	using HapResetPairingsFn = std::function<void()>;

	HapIsPairedFn      hap_is_paired;
	HapReopenPairingFn hap_reopen_pairing;
	HapResetPairingsFn hap_reset_pairings;
	ChActiveFn         ch_active;
	ChRemainingFn      ch_remaining;
	bool               hap_pairing_timed_out = false;

	std::string  hap_setup_uri;
	uint8_t      moist_threshold = 50;
};
