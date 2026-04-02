#pragma once
#include <stdint.h>


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

	bool isSchedEnabled() const        { return m_sched_enabled; }
	void setSchedEnabled(bool enabled) { m_sched_enabled = enabled; }

private:
	const char* m_name;
	time_evt_t  m_evt[EVT_PER_CH];
	bool        m_sched_enabled = true;
};
