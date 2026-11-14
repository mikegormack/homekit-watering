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
    using MoistureRawFn = std::function<int()>;

    struct MoistureCal { int dry; int wet; uint8_t threshold; bool valid; };
    using MoistureCalGetFn = std::function<MoistureCal()>;
    using MoistureCalSetFn = std::function<void(int dry, int wet)>;
    using MoistureThrSetFn = std::function<void(uint8_t thr)>;
    using SchedGetFn        = std::function<const time_evt_t*(int ch)>;  // returns m_evt[2]
    using SchedSetFn        = std::function<void(int ch, const time_evt_t* evts)>;
    using SchedEnabledGetFn = std::function<bool(int ch)>;
    using SchedEnabledSetFn = std::function<void(int ch, bool enabled)>;
    using LogGetFn          = std::function<int(char *buf, int len)>;    // returns bytes written
    using LogClearFn        = std::function<void()>;
    using TrendGetFn        = std::function<int(char *buf, int len)>;
    using TrendClearFn      = std::function<void()>;
    using SoilTempFn        = std::function<bool(float *temp_c)>;  // false = no reading

    ValveStatusFn    valve_status;
    ValveRunFn       valve_run;
    ValveStopFn      valve_stop;
    MoistureFn       moisture;
    MoistureRawFn    moisture_raw;
    SoilTempFn       soil_temp;
    MoistureCalGetFn moisture_cal_get;
    MoistureCalSetFn moisture_cal_set;
    MoistureThrSetFn moisture_thr_set;
    SchedGetFn       sched_get;
    SchedSetFn       sched_set;
    SchedEnabledGetFn sched_enabled_get;
    SchedEnabledSetFn sched_enabled_set;
    LogGetFn         log_get;
    LogClearFn       log_clear;
    TrendGetFn       trend_get;
    TrendClearFn     trend_clear;

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
    static esp_err_t moist_cal_get_handler(httpd_req_t *req);
    static esp_err_t moist_cal_set_handler(httpd_req_t *req);
    static esp_err_t moist_thr_set_handler(httpd_req_t *req);
    static esp_err_t trend_get_handler(httpd_req_t *req);
    static esp_err_t trend_clear_handler(httpd_req_t *req);
};
