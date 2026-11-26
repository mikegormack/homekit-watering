#pragma once
#include <stdint.h>
#include <functional>


#define EVT_PER_CH  2

typedef struct
{
	uint8_t hour;
	uint8_t min;
	uint8_t duration;
} time_evt_t;

class OutputChannel
{
public:
	OutputChannel(const char* name);

	~OutputChannel();

	void save();
	void load();

	const time_evt_t* getEvents() const { return m_evt; }
	void              setEvent(int i, const time_evt_t& evt) { m_evt[i] = evt; }
	const char*       getName() const { return m_name; }

	// Automatic mode: scheduled runs happen only while enabled. Changes made from any place
	// (web page, screen, HomeKit) are reported through the change callback.
	using ChangeFn = std::function<void()>;
	bool isSchedEnabled() const { return m_sched_enabled; }
	void setSchedEnabled(bool enabled)
	{
		bool changed    = enabled != m_sched_enabled;
		m_sched_enabled = enabled;
		if (changed && m_sched_changed_fn)
			m_sched_changed_fn();
	}
	void setSchedChangedFn(ChangeFn fn) { m_sched_changed_fn = fn; }

	// Soil probe (Modbus address, 0 = none) whose moisture can skip scheduled runs of this zone,
	// and the moisture % at or above which a scheduled run is skipped.
	uint8_t getProbeAddr() const         { return m_probe_addr; }
	void    setProbeAddr(uint8_t addr)   { m_probe_addr = addr; }
	uint8_t getMoistThreshold() const    { return m_moist_thr; }
	void    setMoistThreshold(uint8_t v) { m_moist_thr = v > 100 ? 100 : v; }

private:
	const char* m_name;
	time_evt_t  m_evt[EVT_PER_CH];
	bool        m_sched_enabled = true;
	uint8_t     m_probe_addr    = 0;
	uint8_t     m_moist_thr     = 50;
	ChangeFn    m_sched_changed_fn;
};
