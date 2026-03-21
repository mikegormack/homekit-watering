#pragma once

#include <esp_http_server.h>

class OtaServer
{
public:
    OtaServer()  = default;
    ~OtaServer() = default;

    void start();
    void stop();

private:
    httpd_handle_t m_server = nullptr;

    static esp_err_t index_handler(httpd_req_t *req);
    static esp_err_t update_handler(httpd_req_t *req);
};
