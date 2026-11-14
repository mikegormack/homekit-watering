#pragma once

#include <esp_http_server.h>
#include <functional>

#include <ProbeManager.h>

// Web page /probes: probe list and live readings, calibration, address changes, bus scan,
// zone-to-probe assignment and probe firmware updates.
class ProbeServer
{
public:
    static constexpr int NUM_ZONES = 2;

    struct Zone
    {
        uint8_t probe_addr;  // 0 = none
        uint8_t threshold;   // %
    };

    using ZoneGetFn = std::function<Zone(int ch)>;
    using ZoneSetFn = std::function<void(int ch, const Zone &z)>;

    ZoneGetFn zone_get;
    ZoneSetFn zone_set;

    explicit ProbeServer(ProbeManager &probes) : m_probes(probes) {}

    void start(httpd_handle_t server);

private:
    ProbeManager  &m_probes;
    httpd_handle_t m_server = nullptr;

    static esp_err_t page_handler(httpd_req_t *req);
    static esp_err_t list_handler(httpd_req_t *req);
    static esp_err_t action_handler(httpd_req_t *req);
    static esp_err_t settings_handler(httpd_req_t *req);
    static esp_err_t fw_handler(httpd_req_t *req);
};
