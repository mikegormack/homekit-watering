
#include "ValveChannel.h"
#include "Scheduler.h"

#include <string.h>
#include <esp_log.h>

static const char *TAG = "ValveChannel";

ValveChannel::ValveChannel(int ch_num, const char *hap_name, const char *nvs_key,
                           uint32_t default_duration_s)
    : OutputChannel(nvs_key),
      m_ch_num(ch_num),
      m_hap_name(hap_name),
      m_set_duration(default_duration_s),
      m_service(nullptr),
      m_active(false),
      m_remaining(0),
      m_timer(nullptr)
{
}

ValveChannel::~ValveChannel()
{
    if (m_timer)
    {
        esp_timer_stop(m_timer);
        esp_timer_delete(m_timer);
    }
}

hap_serv_t *ValveChannel::createService()
{
    m_service = hap_serv_valve_create(0, 0, 1); // inactive, not-in-use, irrigation type
    hap_serv_add_char(m_service, hap_char_name_create((char *)m_hap_name));
    hap_serv_add_char(m_service, hap_char_set_duration_create(m_set_duration));
    hap_serv_add_char(m_service, hap_char_remaining_duration_create(0));

    if (m_moisture_fn)
        hap_serv_add_char(m_service, hap_char_water_level_create(0));

    hap_serv_set_priv(m_service, this);
    hap_serv_set_write_cb(m_service, hap_write_cb);
    hap_serv_set_read_cb(m_service, hap_read_cb);

    esp_timer_create_args_t timer_args = {
        .callback              = timer_cb,
        .arg                   = this,
        .dispatch_method       = ESP_TIMER_TASK,
        .name                  = "valve_run",
        .skip_unhandled_events = true,
    };
    esp_timer_create(&timer_args, &m_timer);

    ESP_LOGI(TAG, "CH%d service created (default %lus)", m_ch_num, (unsigned long)m_set_duration);
    return m_service;
}

void ValveChannel::registerWithScheduler(Scheduler &sched)
{
    for (int i = 0; i < EVT_PER_CH; i++)
    {
        sched.addEvent(&m_evt[i].hour, &m_evt[i].min, [this, i]() {
            scheduledActivate((uint32_t)m_evt[i].duration * 60);
        });
    }
    ESP_LOGI(TAG, "CH%d registered %d schedule events", m_ch_num, EVT_PER_CH);
}

void ValveChannel::scheduledActivate(uint32_t duration_s)
{
    if (m_active)
        return;
    if (duration_s == 0)
    {
        ESP_LOGI(TAG, "CH%d skip schedule: duration is 0", m_ch_num);
        return;
    }
    if (m_moist_threshold && m_moisture_fn)
    {
        float moisture = m_moisture_fn();
        if (moisture >= (float)*m_moist_threshold)
        {
            ESP_LOGI(TAG, "CH%d skip schedule: moisture %.1f >= threshold %d",
                     m_ch_num, moisture, (int)*m_moist_threshold);
            return;
        }
    }
    onActivate(duration_s);
}

void ValveChannel::onActivate(uint32_t duration_s)
{
    m_active    = true;
    m_remaining = duration_s;

    hap_char_t *active_c  = hap_serv_get_char_by_uuid(m_service, HAP_CHAR_UUID_ACTIVE);
    hap_char_t *in_use_c  = hap_serv_get_char_by_uuid(m_service, HAP_CHAR_UUID_IN_USE);
    hap_char_t *rem_dur_c = hap_serv_get_char_by_uuid(m_service, HAP_CHAR_UUID_REMAINING_DURATION);

    hap_val_t val;
    val.b = true;
    hap_char_update_val(active_c, &val);
    hap_char_update_val(in_use_c, &val);
    val.u = duration_s;
    hap_char_update_val(rem_dur_c, &val);

    if (m_output_fn)
        m_output_fn(true);

    if (duration_s > 0)
    {
        esp_timer_stop(m_timer);
        esp_timer_start_periodic(m_timer, 1000000 /* 1s */);
    }

    ESP_LOGI(TAG, "CH%d activated (%lus)", m_ch_num, (unsigned long)duration_s);
}

void ValveChannel::onDeactivate()
{
    esp_timer_stop(m_timer);
    m_active    = false;
    m_remaining = 0;

    if (m_output_fn)
        m_output_fn(false);

    hap_char_t *in_use_c  = hap_serv_get_char_by_uuid(m_service, HAP_CHAR_UUID_IN_USE);
    hap_char_t *rem_dur_c = hap_serv_get_char_by_uuid(m_service, HAP_CHAR_UUID_REMAINING_DURATION);

    hap_val_t val;
    val.b = false;
    hap_char_update_val(in_use_c, &val);
    val.u = 0;
    hap_char_update_val(rem_dur_c, &val);

    ESP_LOGI(TAG, "CH%d deactivated", m_ch_num);
}

void ValveChannel::timer_cb(void *arg)
{
    ValveChannel *self = static_cast<ValveChannel *>(arg);

    if (self->m_remaining > 0)
        self->m_remaining = self->m_remaining - 1;

    hap_char_t *rem_dur_c = hap_serv_get_char_by_uuid(self->m_service, HAP_CHAR_UUID_REMAINING_DURATION);
    hap_val_t val;
    val.u = self->m_remaining;
    hap_char_update_val(rem_dur_c, &val);

    if (self->m_remaining == 0)
    {
        esp_timer_stop(self->m_timer);
        self->m_active = false;

        if (self->m_output_fn)
            self->m_output_fn(false);

        hap_char_t *active_c = hap_serv_get_char_by_uuid(self->m_service, HAP_CHAR_UUID_ACTIVE);
        hap_char_t *in_use_c = hap_serv_get_char_by_uuid(self->m_service, HAP_CHAR_UUID_IN_USE);
        val.b = false;
        hap_char_update_val(active_c, &val);
        hap_char_update_val(in_use_c, &val);

        ESP_LOGI(TAG, "CH%d run complete", self->m_ch_num);
    }
}

int ValveChannel::hap_read_cb(hap_char_t *hc, hap_status_t *status, void *serv_priv, void *read_priv)
{
    ValveChannel *self = static_cast<ValveChannel *>(serv_priv);
    const char   *uuid = hap_char_get_type_uuid(hc);
    *status = HAP_STATUS_SUCCESS;

    hap_val_t val;
    if (!strcmp(uuid, HAP_CHAR_UUID_ACTIVE) || !strcmp(uuid, HAP_CHAR_UUID_IN_USE))
    {
        val.b = self->m_active;
        hap_char_update_val(hc, &val);
    }
    else if (!strcmp(uuid, HAP_CHAR_UUID_REMAINING_DURATION))
    {
        val.u = self->m_remaining;
        hap_char_update_val(hc, &val);
    }
    else if (!strcmp(uuid, HAP_CHAR_UUID_WATER_LEVEL) && self->m_moisture_fn)
    {
        val.f = self->m_moisture_fn();
        hap_char_update_val(hc, &val);
    }

    return HAP_SUCCESS;
}

int ValveChannel::hap_write_cb(hap_write_data_t *write_data, int count, void *serv_priv, void *write_priv)
{
    ValveChannel *self = static_cast<ValveChannel *>(serv_priv);

    for (int i = 0; i < count; i++)
    {
        hap_write_data_t *w    = &write_data[i];
        const char       *uuid = hap_char_get_type_uuid(w->hc);

        if (!strcmp(uuid, HAP_CHAR_UUID_ACTIVE))
        {
            hap_char_update_val(w->hc, &w->val);
            if (w->val.b)
                self->onActivate(self->m_set_duration);
            else
                self->onDeactivate();
            *(w->status) = HAP_STATUS_SUCCESS;
        }
        else if (!strcmp(uuid, HAP_CHAR_UUID_SET_DURATION))
        {
            self->m_set_duration = w->val.u;
            hap_char_update_val(w->hc, &w->val);
            *(w->status) = HAP_STATUS_SUCCESS;
        }
        else
        {
            *(w->status) = HAP_STATUS_RES_ABSENT;
        }
    }
    return HAP_SUCCESS;
}
