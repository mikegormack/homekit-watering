#pragma once

#include <functional>

#include <hap.h>
#include <hap_apple_servs.h>
#include <hap_apple_chars.h>
#include <esp_timer.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

#include <OutputChannel.h>

class Scheduler;
class EventLog;

class ValveChannel : public OutputChannel
{
public:
    using MoistureFn    = std::function<float()>;
    using MoistThrFn    = std::function<uint8_t()>;
    using OutputFn      = std::function<void(bool)>; // true = on, false = off

    // hap_name: shown in Home app; nvs_key: used for NVS schedule persistence
    ValveChannel(int ch_num, const char *hap_name, const char *nvs_key,
                 uint32_t default_duration_s = 600);
    ~ValveChannel();

    void setMoistureGetter(MoistureFn fn)    { m_moisture_fn = fn; }
    void setMoistureThreshold(MoistThrFn fn) { m_moist_threshold_fn = fn; }
    void setOutputFn(OutputFn fn)            { m_output_fn = fn; }
    void setEventLog(EventLog &log)          { m_log = &log; }

    // Register the two scheduled events (from OutputChannel::m_evt) with a Scheduler
    void registerWithScheduler(Scheduler &sched);

    // Creates and returns the HAP service; call once before hap_add_accessory()
    hap_serv_t *createService();

    bool     isActive()    const { return m_active; }
    uint32_t getRemaining() const { return m_remaining; }

    // Called by Scheduler when a scheduled event fires
    void scheduledActivate(uint32_t duration_s);

    // Manual button control
    void manualRun(uint32_t duration_s);
    void manualStop();

private:
    int                m_ch_num;
    const char        *m_hap_name;
    uint32_t           m_set_duration;
    hap_serv_t        *m_service;
    bool               m_active;
    uint32_t           m_remaining;
    esp_timer_handle_t m_timer;
    SemaphoreHandle_t  m_mutex;
    MoistureFn         m_moisture_fn;
    OutputFn           m_output_fn;
    MoistThrFn         m_moist_threshold_fn;
    EventLog          *m_log;

    void onActivate(uint32_t duration_s);
    void onDeactivate();

    static void timer_cb(void *arg);
    static int  hap_read_cb(hap_char_t *hc, hap_status_t *status, void *serv_priv, void *read_priv);
    static int  hap_write_cb(hap_write_data_t *write_data, int count, void *serv_priv, void *write_priv);
};
