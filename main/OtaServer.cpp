
#include "OtaServer.h"
#include "WebLog.h"

#include <esp_log.h>
#include <stdio.h>
#include <esp_ota_ops.h>
#include <esp_timer.h>
#include <esp_app_format.h>
#include <esp_system.h>

static const char *TAG = "OtaServer";

static const char *SYSTEM_HTML = R"html(<!DOCTYPE html>
<html>
<head>
  <meta charset="utf-8">
  <meta name="viewport" content="width=device-width,initial-scale=1">
  <title>System</title>
  <style>
    body  { font-family: sans-serif; max-width: 600px; margin: 24px auto 40px; padding: 0 16px; }
    .nav  { margin: 0 0 8px; }
    a     { color: #2a5db0; }
    h1    { font-size: 1.4em; }
    h2    { font-size: 1.1em; border-bottom: 1px solid #ccc; padding-bottom: 4px; margin-top: 0; }
    .card { border: 1px solid #ddd; border-radius: 8px; padding: 16px; margin-bottom: 16px; }
    table { border-collapse: collapse; width: 100%; }
    td    { padding: 5px 8px 5px 0; }
    td:first-child { color: #666; width: 45%; }
    input[type=file] { display: block; margin: 12px 0; }
    button { padding: 8px 16px; cursor: pointer; border-radius: 4px; border: 1px solid #aaa; }
    button.stop { background: #fee; border-color: #c66; }
    progress { display: none; width: 100%; margin-top: 12px; }
    #status, #rmsg { margin-top: 8px; font-weight: bold; }
  </style>
</head>
<body>
  <p class="nav"><a href="/">&larr; Home</a></p>
  <h1>System</h1>

  <div class="card">
    <h2>Status</h2>
    <table><tbody id="info"><tr><td>Loading...</td><td></td></tr></tbody></table>
  </div>

  <div class="card">
    <h2>Firmware update</h2>
    <p style="margin:0;color:#555;font-size:0.9em">Upload <code>build/sprinkler.bin</code>. The controller restarts with the new firmware.</p>
    <input type="file" id="file" accept=".bin">
    <button id="btn" onclick="upload()">Upload &amp; update</button>
    <progress id="progress" max="100" value="0"></progress>
    <div id="status"></div>
  </div>

  <div class="card">
    <h2>Restart</h2>
    <p style="margin:0 0 12px;color:#555;font-size:0.9em">Running valves stop. Probes added or removed show up in HomeKit after a restart.</p>
    <button class="stop" onclick="restart()">Restart controller</button>
    <div id="rmsg"></div>
  </div>

  <script>
    const RST = { 0:'unknown', 1:'power on', 2:'reset pin', 3:'software restart', 4:'crash (panic)',
                  5:'interrupt watchdog', 6:'task watchdog', 7:'watchdog', 8:'deep sleep', 9:'brownout', 10:'SDIO' };
    function dur(s) {
      const d = Math.floor(s / 86400), h = Math.floor(s % 86400 / 3600), m = Math.floor(s % 3600 / 60);
      return (d ? d + ' d ' : '') + (d || h ? h + ' h ' : '') + m + ' min';
    }
    function kb(b) { return (b / 1024).toFixed(0) + ' KB'; }
    function refresh() {
      fetch('/api/system').then(r => r.json()).then(d => {
        const rows = [
          ['Firmware', d.version],
          ['Built', d.date + ' ' + d.time],
          ['Running from', d.partition],
          ['ESP-IDF', d.idf],
          ['Uptime', dur(d.uptime_s)],
          ['Last restart', RST[d.reset_reason] || ('reason ' + d.reset_reason)],
          ['Free memory (lowest)', kb(d.heap_free) + ' (' + kb(d.heap_min) + ')'],
        ];
        if (d.last_crash) rows.push(['Last crash', '<span style="color:#c00;word-break:break-all">' +
          d.last_crash.replace(/&/g, '&amp;').replace(/</g, '&lt;') + '</span>']);
        document.getElementById('info').innerHTML = rows.map(r => '<tr><td>' + r[0] + '</td><td>' + r[1] + '</td></tr>').join('');
      }).catch(() => {});
    }
    function restart() {
      if (!confirm('Restart the controller? Running valves stop.')) return;
      fetch('/api/system/restart', { method: 'POST' }).then(r => r.text()).then(t => {
        document.getElementById('rmsg').textContent = t;
        setTimeout(() => location.reload(), 8000);
      });
    }
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
    refresh();
    setInterval(refresh, 5000);
  </script>
</body>
</html>)html";

// ---------------------------------------------------------------------------

void OtaServer::start(httpd_handle_t server)
{
    m_server = server;

    httpd_uri_t uris[] = {
        { "/system",             HTTP_GET,  index_handler,    nullptr },
        { "/api/system",         HTTP_GET,  info_handler,     nullptr },
        { "/api/system/restart", HTTP_POST, restart_handler,  nullptr },
        { "/update",             HTTP_GET,  redirect_handler, nullptr },  // old page address
        { "/update",             HTTP_POST, update_handler,   this    },
        { "/version",            HTTP_GET,  version_handler,  nullptr },
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
    httpd_resp_sendstr(req, SYSTEM_HTML);
    return ESP_OK;
}

esp_err_t OtaServer::redirect_handler(httpd_req_t *req)
{
    httpd_resp_set_status(req, "302 Found");
    httpd_resp_set_hdr(req, "Location", "/system");
    return httpd_resp_send(req, nullptr, 0);
}

esp_err_t OtaServer::info_handler(httpd_req_t *req)
{
    const esp_app_desc_t  *desc = esp_app_get_description();
    const esp_partition_t *part = esp_ota_get_running_partition();
    // Last crash text, JSON-escaped
    char        crash[600];
    size_t      k = 0;
    for (const char *p = weblog::last_crash(); *p && k < sizeof(crash) - 2; p++)
    {
        if (*p == '"' || *p == '\\')
            crash[k++] = '\\';
        crash[k++] = (*p >= ' ') ? *p : ' ';
    }
    crash[k] = '\0';

    char buf[1024];
    snprintf(buf, sizeof(buf),
             "{\"version\":\"%s\",\"date\":\"%s\",\"time\":\"%s\",\"idf\":\"%s\",\"partition\":\"%s\","
             "\"uptime_s\":%lld,\"reset_reason\":%d,\"heap_free\":%lu,\"heap_min\":%lu,\"last_crash\":\"%s\"}",
             desc->version, desc->date, desc->time, desc->idf_ver, part ? part->label : "?",
             (long long)(esp_timer_get_time() / 1000000), (int)esp_reset_reason(),
             (unsigned long)esp_get_free_heap_size(), (unsigned long)esp_get_minimum_free_heap_size(), crash);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    return httpd_resp_sendstr(req, buf);
}

esp_err_t OtaServer::restart_handler(httpd_req_t *req)
{
    ESP_LOGW(TAG, "Restart requested from the web page");
    httpd_resp_sendstr(req, "Restarting - this page reloads in a few seconds");

    // Restart after the reply has gone out.
    static esp_timer_handle_t t = nullptr;
    if (!t)
    {
        esp_timer_create_args_t args = {};
        args.callback                = [](void *) { esp_restart(); };
        args.name                    = "web_restart";
        esp_timer_create(&args, &t);
    }
    esp_timer_start_once(t, 1000000);
    return ESP_OK;
}

esp_err_t OtaServer::update_handler(httpd_req_t *req)
{
    ESP_LOGI(TAG, "OTA upload started, content_len=%d", req->content_len);

    OtaServer *srv = static_cast<OtaServer *>(req->user_ctx);
    if (srv && srv->partition_busy && srv->partition_busy())
    {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "A probe firmware update is running, try again later");
        return ESP_FAIL;
    }

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

    uint16_t new_ver = 0;
    esp_app_desc_t new_desc;
    if (esp_ota_get_partition_description(update_partition, &new_desc) == ESP_OK)
    {
        int ma = 0, mi = 0, pa = 0;
        sscanf(new_desc.version, "%d.%d.%d", &ma, &mi, &pa);
        new_ver = (uint16_t)(ma * 10000 + mi * 100 + pa);
    }

    OtaServer *self = static_cast<OtaServer *>(req->user_ctx);
    if (self && self->on_ota_complete)
        self->on_ota_complete(new_ver);

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
