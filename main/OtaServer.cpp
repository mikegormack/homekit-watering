
#include "OtaServer.h"

#include <esp_log.h>
#include <esp_ota_ops.h>
#include <esp_timer.h>
#include <esp_app_format.h>

static const char *TAG = "OtaServer";

static const char *INDEX_HTML = R"html(<!DOCTYPE html>
<html>
<head>
  <meta charset="utf-8">
  <meta name="viewport" content="width=device-width,initial-scale=1">
  <title>Firmware Update</title>
  <style>
    body { font-family: sans-serif; max-width: 480px; margin: 40px auto; padding: 0 16px; }
    h1   { font-size: 1.4em; }
    input[type=file] { display: block; margin: 16px 0; }
    button { padding: 10px 24px; font-size: 1em; cursor: pointer; }
    progress { display: none; width: 100%; margin-top: 16px; }
    #status { margin-top: 8px; font-weight: bold; }
  </style>
</head>
<body>
  <h1>Firmware Update</h1>
  <div id="version" style="margin-bottom:12px;color:#555;font-size:0.9em;"></div>
  <input type="file" id="file" accept=".bin">
  <button id="btn" onclick="upload()">Upload &amp; Update</button>
  <progress id="progress" max="100" value="0"></progress>
  <div id="status"></div>
  <script>
    fetch('/version').then(r => r.text()).then(v => { document.getElementById('version').innerText = 'Current firmware: ' + v; });
    function upload() {
      const file = document.getElementById('file').files[0];
      if (!file) { document.getElementById('status').innerText = 'Select a .bin file first.'; return; }
      const progress = document.getElementById('progress');
      const status   = document.getElementById('status');
      document.getElementById('btn').disabled = true;
      progress.style.display = 'block';
      progress.value = 0;
      status.innerText = 'Uploading...';
      const xhr = new XMLHttpRequest();
      xhr.open('POST', '/update');
      xhr.setRequestHeader('Content-Type', 'application/octet-stream');
      xhr.upload.onprogress = function(e) {
        if (e.lengthComputable) {
          const pct = Math.round(e.loaded / e.total * 100);
          progress.value = pct;
          status.innerText = 'Uploading... ' + pct + '%';
        }
      };
      xhr.onload = function() {
        progress.value = 100;
        status.innerText = xhr.responseText;
        document.getElementById('btn').disabled = false;
      };
      xhr.onerror = function() {
        status.innerText = 'Upload failed.';
        document.getElementById('btn').disabled = false;
      };
      xhr.send(file);
    }
  </script>
</body>
</html>)html";

// ---------------------------------------------------------------------------

void OtaServer::start(httpd_handle_t server)
{
    m_server = server;

    httpd_uri_t uris[] = {
        { "/update",  HTTP_GET,  index_handler,   nullptr },
        { "/update",  HTTP_POST, update_handler,  this    },
        { "/version", HTTP_GET,  version_handler, nullptr },
    };
    for (auto &u : uris)
        httpd_register_uri_handler(m_server, &u);

    ESP_LOGI(TAG, "OTA handlers registered");
}

void OtaServer::stop()
{
    // handlers are deregistered when the shared httpd stops
    m_server = nullptr;
}

// ---------------------------------------------------------------------------

esp_err_t OtaServer::version_handler(httpd_req_t *req)
{
    const esp_app_desc_t *desc = esp_app_get_description();
    httpd_resp_set_type(req, "text/plain");
    httpd_resp_sendstr(req, desc->version);
    return ESP_OK;
}

esp_err_t OtaServer::index_handler(httpd_req_t *req)
{
    httpd_resp_set_type(req, "text/html");
    httpd_resp_sendstr(req, INDEX_HTML);
    return ESP_OK;
}

esp_err_t OtaServer::update_handler(httpd_req_t *req)
{
    ESP_LOGI(TAG, "OTA upload started, content_len=%d", req->content_len);

    const esp_partition_t *update_partition = esp_ota_get_next_update_partition(nullptr);
    if (!update_partition)
    {
        ESP_LOGE(TAG, "No OTA partition found");
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "No OTA partition");
        return ESP_FAIL;
    }

    esp_ota_handle_t ota_handle;
    esp_err_t err = esp_ota_begin(update_partition, OTA_WITH_SEQUENTIAL_WRITES, &ota_handle);
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "esp_ota_begin err %d", err);
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "OTA begin failed");
        return ESP_FAIL;
    }

    char buf[4096];
    int remaining = req->content_len;

    while (remaining > 0)
    {
        int to_recv = remaining < (int)sizeof(buf) ? remaining : (int)sizeof(buf);
        int received = httpd_req_recv(req, buf, to_recv);
        if (received <= 0)
        {
            ESP_LOGE(TAG, "Receive error %d", received);
            esp_ota_abort(ota_handle);
            httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Receive failed");
            return ESP_FAIL;
        }

        err = esp_ota_write(ota_handle, buf, received);
        if (err != ESP_OK)
        {
            ESP_LOGE(TAG, "esp_ota_write err %d", err);
            esp_ota_abort(ota_handle);
            httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "OTA write failed");
            return ESP_FAIL;
        }

        remaining -= received;
        ESP_LOGI(TAG, "OTA progress: %d / %d bytes", req->content_len - remaining, req->content_len);
    }

    err = esp_ota_end(ota_handle);
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "esp_ota_end err %d", err);
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "OTA validation failed");
        return ESP_FAIL;
    }

    err = esp_ota_set_boot_partition(update_partition);
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "esp_ota_set_boot_partition err %d", err);
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Set boot partition failed");
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "OTA complete, rebooting");

    OtaServer *self = static_cast<OtaServer *>(req->user_ctx);
    if (self && self->on_ota_complete)
        self->on_ota_complete();

    httpd_resp_sendstr(req, "Update successful. Device rebooting...");

    // Reboot after a short delay to allow the HTTP response to be sent
    esp_timer_handle_t reboot_timer;
    esp_timer_create_args_t timer_args = {
        .callback        = [](void *) { esp_restart(); },
        .arg             = nullptr,
        .dispatch_method = ESP_TIMER_TASK,
        .name            = "ota_reboot",
        .skip_unhandled_events = false
    };
    esp_timer_create(&timer_args, &reboot_timer);
    esp_timer_start_once(reboot_timer, 1500000); // 1.5 seconds

    return ESP_OK;
}
