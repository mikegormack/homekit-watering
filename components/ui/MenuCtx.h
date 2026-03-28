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

	// --- type aliases -------------------------------------------------------
	using StartProvFn      = std::function<std::unique_ptr<uint8_t[]>()>;
	using StopProvFn       = std::function<void()>;
	using ProvStatusFn     = std::function<ProvStatus()>;
	using WifiInfoFn       = std::function<WifiInfo()>;
	using RssiGetterFn     = std::function<int8_t()>;
	using MoistGetterFn    = std::function<uint8_t()>;
	using MoistRawGetterFn = std::function<int()>;
	using MoistCalSetterFn = std::function<void(int dry, int wet)>;
	using MoistThrGetterFn = std::function<uint8_t()>;
	using MoistThrSetterFn = std::function<void(uint8_t)>;
	using TzGetterFn       = std::function<int()>;
	using TzSetterFn       = std::function<void(int)>;
	using HapIsPairedFn      = std::function<bool()>;
	using HapReopenPairingFn = std::function<void()>;
	using HapResetPairingsFn = std::function<void()>;
	using ChActiveFn    = std::function<bool(int ch)>;
	using ChRemainingFn = std::function<uint32_t(int ch)>;
	using ChToggleFn    = std::function<void(int ch, uint32_t dur_s)>;

	// --- grouped sub-structs ------------------------------------------------
	struct Wifi
	{
		StartProvFn    start_prov;
		StopProvFn     stop_prov;
		ProvStatusFn   prov_status;
		WifiInfoFn     wifi_info;
		RssiGetterFn   rssi_getter;
	} wifi;

	struct Moisture
	{
		MoistGetterFn    getter;
		MoistRawGetterFn raw_getter;
		MoistCalSetterFn cal_setter;
		MoistThrGetterFn thr_getter;
		MoistThrSetterFn thr_setter;
	} moisture;

	struct Hap
	{
		HapIsPairedFn      is_paired;
		HapReopenPairingFn reopen_pairing;
		HapResetPairingsFn reset_pairings;
		bool               pairing_timed_out = false;
		std::string        setup_uri;
	} hap;

	struct Valves
	{
		ChActiveFn    active;
		ChRemainingFn remaining;
		ChToggleFn    toggle;
	} valves;

	struct Settings
	{
		TzGetterFn tz_getter;
		TzSetterFn tz_setter;
	} settings;

	// Pointers to the two valve channels (as OutputChannel for schedule editing)
	OutputChannel *out_ch[2] = {nullptr, nullptr};
};
