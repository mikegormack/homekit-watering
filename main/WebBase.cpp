
#include "WebBase.h"
#include <esp_log.h>

static const char *TAG = "WebBase";

static const char *BASE_HTML = R"html(<!DOCTYPE html>
<html>
<head>
  <meta charset="utf-8">
  <meta name="viewport" content="width=device-width,initial-scale=1">
  <title>Sprinkler</title>
  <style>
    body { font-family: sans-serif; max-width: 400px; margin: 60px auto; padding: 0 16px; text-align: center; }
    h1   { font-size: 1.6em; margin-bottom: 32px; }
    a.btn {
      display: block; padding: 16px; margin: 12px 0;
      background: #f5f5f5; border: 1px solid #ddd; border-radius: 8px;
      text-decoration: none; color: #222; font-size: 1.1em;
    }
    a.btn:hover { background: #e8e8e8; }
  </style>
</head>
<body>
  <h1>&#127807; Sprinkler</h1>
  <a class="btn" href="/control">Valve Control &amp; Schedule</a>
  <a class="btn" href="/update">Firmware Update</a>
</body>
</html>)html";

void WebBase::start()
{
    httpd_config_t config    = HTTPD_DEFAULT_CONFIG();
    config.server_port       = 8080;
    config.max_open_sockets  = 2;
    config.stack_size        = 8192;
    config.recv_wait_timeout = 30;   // needed for large OTA uploads
    config.max_uri_handlers  = 16;   // base + OTA (3) + control (8)

    if (httpd_start(&m_server, &config) != ESP_OK)
    {
        ESP_LOGE(TAG, "Failed to start HTTP server");
        return;
    }

    httpd_uri_t uri = {
        .uri      = "/",
        .method   = HTTP_GET,
        .handler  = index_handler,
        .user_ctx = nullptr,
    };
    httpd_register_uri_handler(m_server, &uri);

    ESP_LOGI(TAG, "HTTP server started on port 8080");
}

void WebBase::stop()
{
    if (m_server)
    {
        httpd_stop(m_server);
        m_server = nullptr;
    }
}

esp_err_t WebBase::index_handler(httpd_req_t *req)
{
    httpd_resp_set_type(req, "text/html");
    httpd_resp_sendstr(req, BASE_HTML);
    return ESP_OK;
}
