#pragma once

#include <functional>
#include <memory>
#include <string>
#include <vector>

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

	// Snapshot of one RS485 soil probe for display.
	struct ProbeSummary
	{
		uint8_t addr;
		char    name[16];
		bool    online;
		bool    in_bootloader;
		bool    calibrated;
		bool    temp_ok;
		float   moisture;  // %
		float   temp;      // degC
	};

	// --- type aliases -------------------------------------------------------
	using StartProvFn      = std::function<std::unique_ptr<uint8_t[]>()>;
	using StopProvFn       = std::function<void()>;
	using ProvStatusFn     = std::function<ProvStatus()>;
	using WifiInfoFn       = std::function<WifiInfo()>;
	using RssiGetterFn     = std::function<int8_t()>;
	using ProbeListFn      = std::function<std::vector<ProbeSummary>()>;
	using ChMoistureFn     = std::function<float(int ch)>;  // % from the zone's probe, < 0 if unavailable
	using ChTempFn         = std::function<bool(int ch, float *temp_c)>;  // zone probe soil temperature
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

	struct Probes
	{
		ProbeListFn  list;
		ChMoistureFn ch_moisture;
		ChTempFn     ch_temp;
	} probes;

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

	// Pointers to the two valve channels (as OutputChannel for schedule, probe and threshold settings)
	OutputChannel *out_ch[2] = {nullptr, nullptr};
};
