#pragma once

#include <esp_http_server.h>
#include <functional>
#include <string>

class WebBase
{
public:
    // HomeKit pairing details for the /homekit page.
    struct HomeKitInfo
    {
        std::string setup_uri;    // X-HM://... payload for the QR code, empty if unknown
        std::string setup_code;   // e.g. 111-22-333, empty if unknown
        int         paired = 0;   // number of paired controllers
        bool        pairing_timed_out = false;
    };
    std::function<HomeKitInfo()> homekit_info;
    std::function<void()>        homekit_reopen_pairing;
    std::function<void()>        homekit_reset_pairings;  // removes all pairings, then the device reboots

    void           start();
    void           stop();
    httpd_handle_t getHandle() const { return m_server; }

private:
    httpd_handle_t m_server = nullptr;

    static esp_err_t index_handler(httpd_req_t *req);
    static esp_err_t homekit_handler(httpd_req_t *req);
    static esp_err_t homekit_pairing_handler(httpd_req_t *req);
    static esp_err_t homekit_reset_handler(httpd_req_t *req);
    static esp_err_t logs_page_handler(httpd_req_t *req);
    static esp_err_t logs_get_handler(httpd_req_t *req);
    static esp_err_t logs_clear_handler(httpd_req_t *req);
};
