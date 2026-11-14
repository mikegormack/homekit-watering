#pragma once

#include <esp_http_server.h>
#include <functional>

class OtaServer
{
public:
    OtaServer()  = default;
    ~OtaServer() = default;

    using OnOtaCompleteFn = std::function<void(uint16_t)>;  // arg = new version encoded as major*10000+minor*100+patch
    OnOtaCompleteFn on_ota_complete;

    // Returns true while the inactive OTA partition is in use for something else (a probe firmware
    // update stages its image there); uploads are refused meanwhile.
    std::function<bool()> partition_busy;

    void start(httpd_handle_t server);  // register handlers on a shared httpd
    void stop();                        // deregisters handlers

private:
    httpd_handle_t m_server = nullptr;

    static esp_err_t index_handler(httpd_req_t *req);
    static esp_err_t update_handler(httpd_req_t *req);
    static esp_err_t version_handler(httpd_req_t *req);
};
