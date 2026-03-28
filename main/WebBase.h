#pragma once

#include <esp_http_server.h>

class WebBase
{
public:
    void           start();
    void           stop();
    httpd_handle_t getHandle() const { return m_server; }

private:
    httpd_handle_t m_server = nullptr;

    static esp_err_t index_handler(httpd_req_t *req);
};
