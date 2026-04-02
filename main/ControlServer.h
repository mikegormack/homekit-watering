#pragma once

#include <esp_http_server.h>
#include <functional>
#include <OutputChannel.h>

class ControlServer
{
public:
    static constexpr int NUM_VALVES = 2;

    struct ValveStatus { bool active; uint32_t remaining_s; };

    using ValveStatusFn = std::function<ValveStatus(int ch)>;
    using ValveRunFn    = std::function<void(int ch, uint32_t dur_s)>;
    using ValveStopFn   = std::function<void(int ch)>;
    using MoistureFn    = std::function<uint8_t()>;
    using SchedGetFn        = std::function<const time_evt_t*(int ch)>;  // returns m_evt[2]
    using SchedSetFn        = std::function<void(int ch, const time_evt_t* evts)>;
    using SchedEnabledGetFn = std::function<bool(int ch)>;
    using SchedEnabledSetFn = std::function<void(int ch, bool enabled)>;
    using LogGetFn          = std::function<int(char *buf, int len)>;    // returns bytes written
    using LogClearFn        = std::function<void()>;

    ValveStatusFn    valve_status;
    ValveRunFn       valve_run;
    ValveStopFn      valve_stop;
    MoistureFn       moisture;
    SchedGetFn       sched_get;
    SchedSetFn       sched_set;
    SchedEnabledGetFn sched_enabled_get;
    SchedEnabledSetFn sched_enabled_set;
    LogGetFn         log_get;
    LogClearFn       log_clear;

    void start(httpd_handle_t server);
    void stop();

private:
    httpd_handle_t m_server = nullptr;

    struct ValveCtx { ControlServer *server; int ch; };

    ValveCtx m_valve_ctx[NUM_VALVES];
    char     m_run_uri[NUM_VALVES][24];
    char     m_stop_uri[NUM_VALVES][24];

    static esp_err_t index_handler(httpd_req_t *req);
    static esp_err_t status_handler(httpd_req_t *req);
    static esp_err_t valve_run_handler(httpd_req_t *req);
    static esp_err_t valve_stop_handler(httpd_req_t *req);
    static esp_err_t sched_get_handler(httpd_req_t *req);
    static esp_err_t sched_post_handler(httpd_req_t *req);
    static esp_err_t log_handler(httpd_req_t *req);
    static esp_err_t log_clear_handler(httpd_req_t *req);
};
