
#include "WebBase.h"
#include "WebLog.h"
#include <esp_log.h>
#include <stdlib.h>
#include <esp_timer.h>
#include <memory>
#include <string.h>
#include <stdio.h>
#include <qrcodegen.h>

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
  <a class="btn" href="/probes">Soil Probes</a>
  <a class="btn" href="/events">Event Log</a>
  <a class="btn" href="/homekit">HomeKit Pairing</a>
  <a class="btn" href="/logs">Device Log</a>
  <a class="btn" href="/system">System &amp; Firmware Update</a>
</body>
</html>)html";

void WebBase::start()
{
    httpd_config_t config    = HTTPD_DEFAULT_CONFIG();
    config.server_port       = 8080;
    config.max_open_sockets  = 5;
    config.stack_size        = 8192;
    config.recv_wait_timeout = 30;   // needed for large OTA uploads
    config.max_uri_handlers  = 40;   // base (7) + system/OTA (6) + control (13) + probes (5), with headroom

    if (httpd_start(&m_server, &config) != ESP_OK)
    {
        ESP_LOGE(TAG, "Failed to start HTTP server");
        return;
    }

    httpd_uri_t uris[] = {
        {"/", HTTP_GET, index_handler, nullptr},
        {"/homekit", HTTP_GET, homekit_handler, this},
        {"/api/homekit/pairing", HTTP_POST, homekit_pairing_handler, this},
        {"/api/homekit/reset", HTTP_POST, homekit_reset_handler, this},
        {"/logs", HTTP_GET, logs_page_handler, nullptr},
        {"/api/logs", HTTP_GET, logs_get_handler, nullptr},
        {"/api/logs/clear", HTTP_POST, logs_clear_handler, nullptr},
    };
    for (auto &u : uris)
        httpd_register_uri_handler(m_server, &u);

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

// ---------------------------------------------------------------------------
// HomeKit pairing page: QR code (drawn here as SVG, no internet needed) and setup code
// ---------------------------------------------------------------------------

static const char *HOMEKIT_HEAD = R"html(<!DOCTYPE html>
<html>
<head>
  <meta charset="utf-8">
  <meta name="viewport" content="width=device-width,initial-scale=1">
  <title>HomeKit Pairing</title>
  <style>
    body { font-family: sans-serif; max-width: 420px; margin: 24px auto 40px; padding: 0 16px; text-align: center; }
    .nav { margin: 0 0 8px; text-align: left; }
    h1   { font-size: 1.4em; }
    .qr  { display: inline-block; background: #fff; padding: 12px; border: 1px solid #ddd; border-radius: 12px; }
    .qr svg { display: block; width: 240px; height: 240px; }
    .code { display: inline-block; margin: 16px 0 8px; padding: 8px 16px; border: 2px solid #222; border-radius: 8px;
            font: 600 1.6em ui-monospace, Menlo, Consolas, monospace; letter-spacing: 0.08em; }
    p    { color: #555; font-size: 0.95em; line-height: 1.4; }
    .ok  { color: #1a8a3a; font-weight: bold; }
    button { padding: 8px 16px; cursor: pointer; border-radius: 4px; border: 1px solid #aaa; }
    a    { color: #2a5db0; }
  </style>
</head>
<body>
  <p class="nav"><a href="/">&larr; Home</a></p>
  <h1>HomeKit Pairing</h1>
)html";

static const char *HOMEKIT_TAIL = R"html(
  <script>
    function reopen() {
      fetch('/api/homekit/pairing', { method: 'POST' }).then(r => r.text())
        .then(t => { document.getElementById('msg').textContent = t; });
    }
    function resetPairing() {
      if (!confirm('Remove the HomeKit pairing?\n\nThe sprinkler restarts as a new, unpaired accessory. ' +
                   'In the Home app it will show "No Response": remove it there too, then add it again ' +
                   'with the code on this page. Rooms, scenes and automations using it are lost.')) return;
      fetch('/api/homekit/reset', { method: 'POST', body: 'reset' }).then(r => r.text()).then(t => {
        document.getElementById('rmsg').textContent = t;
        setTimeout(() => location.reload(), 8000);
      });
    }
  </script>
</body>
</html>)html";

// SVG of a QR code: one path of 1x1 squares, 4-module quiet zone.
static std::string qr_svg(const std::string &text)
{
    auto tmp = std::make_unique<uint8_t[]>(qrcodegen_BUFFER_LEN_MAX);
    auto qr  = std::make_unique<uint8_t[]>(qrcodegen_BUFFER_LEN_MAX);
    if (!qrcodegen_encodeText(text.c_str(), tmp.get(), qr.get(), qrcodegen_Ecc_MEDIUM, qrcodegen_VERSION_MIN,
                              qrcodegen_VERSION_MAX, qrcodegen_Mask_AUTO, true))
        return "";
    int         n = qrcodegen_getSize(qr.get());
    char        buf[48];
    std::string svg;
    svg.reserve(8192);
    snprintf(buf, sizeof(buf), "<svg viewBox=\"0 0 %d %d\" ", n + 8, n + 8);
    svg += buf;
    svg += "xmlns=\"http://www.w3.org/2000/svg\" shape-rendering=\"crispEdges\">"
           "<rect width=\"100%\" height=\"100%\" fill=\"#fff\"/><path fill=\"#000\" d=\"";
    for (int y = 0; y < n; y++)
        for (int x = 0; x < n; x++)
            if (qrcodegen_getModule(qr.get(), x, y))
            {
                snprintf(buf, sizeof(buf), "M%d %dh1v1h-1z", x + 4, y + 4);
                svg += buf;
            }
    svg += "\"/></svg>";
    return svg;
}

esp_err_t WebBase::homekit_handler(httpd_req_t *req)
{
    WebBase                *self = static_cast<WebBase *>(req->user_ctx);
    WebBase::HomeKitInfo    info = self->homekit_info ? self->homekit_info() : WebBase::HomeKitInfo{};
    std::string             body;
    char                    buf[160];

    if (!info.setup_uri.empty())
    {
        std::string svg = qr_svg(info.setup_uri);
        if (!svg.empty())
            body += "<div class=\"qr\">" + svg + "</div><br>";
    }
    if (!info.setup_code.empty())
        body += "<div class=\"code\">" + info.setup_code + "</div>";
    if (info.setup_uri.empty() && info.setup_code.empty())
        body += "<p>The setup code is not stored in this firmware (it comes from the factory NVS partition).</p>";

    if (info.paired > 0)
    {
        snprintf(buf, sizeof(buf), "<p class=\"ok\">Paired with %d controller%s.</p>", info.paired,
                 info.paired == 1 ? "" : "s");
        body += buf;
        body += "<p>To add it again, first remove the Sprinkler accessory in the Home app, or remove the pairing "
                "here. It can then be added with this code.</p>";
        body += "<p><button onclick=\"resetPairing()\">Remove pairing</button> <span id=\"rmsg\"></span></p>";
    }
    else
    {
        body += "<p>Not paired. In the Home app: <b>+</b> &rarr; <b>Add Accessory</b>, then scan the code.</p>";
        if (info.pairing_timed_out)
            body += "<p>The pairing window has closed.</p>";
        body += "<p><button onclick=\"reopen()\">Open pairing window</button> <span id=\"msg\"></span></p>";
    }

    httpd_resp_set_type(req, "text/html");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    httpd_resp_send_chunk(req, HOMEKIT_HEAD, HTTPD_RESP_USE_STRLEN);
    httpd_resp_send_chunk(req, body.c_str(), body.size());
    httpd_resp_send_chunk(req, HOMEKIT_TAIL, HTTPD_RESP_USE_STRLEN);
    httpd_resp_send_chunk(req, nullptr, 0);
    return ESP_OK;
}

esp_err_t WebBase::homekit_pairing_handler(httpd_req_t *req)
{
    WebBase *self = static_cast<WebBase *>(req->user_ctx);
    if (self->homekit_info && self->homekit_info().paired > 0)
    {
        httpd_resp_sendstr(req, "Already paired - remove it in the Home app first");
        return ESP_OK;
    }
    if (self->homekit_reopen_pairing)
        self->homekit_reopen_pairing();
    httpd_resp_sendstr(req, "Pairing window open");
    return ESP_OK;
}

esp_err_t WebBase::homekit_reset_handler(httpd_req_t *req)
{
    WebBase *self = static_cast<WebBase *>(req->user_ctx);

    // Body "reset" guards against an accidental empty POST.
    char body[16] = {};
    int  n        = httpd_req_recv(req, body, sizeof(body) - 1);
    if (n <= 0 || strncmp(body, "reset", 5) != 0)
    {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Missing confirmation");
        return ESP_FAIL;
    }
    if (!self->homekit_reset_pairings)
    {
        httpd_resp_sendstr(req, "Not available");
        return ESP_OK;
    }

    ESP_LOGW(TAG, "HomeKit pairings reset from the web page");
    httpd_resp_sendstr(req, "Pairing removed - restarting, this page reloads shortly");

    // Reset (and the reboot it triggers) after the reply has gone out.
    static esp_timer_handle_t t = nullptr;
    if (!t)
    {
        esp_timer_create_args_t args = {};
        args.callback                = [](void *arg) { (*static_cast<std::function<void()> *>(arg))(); };
        args.arg                     = &self->homekit_reset_pairings;
        args.name                    = "hap_reset";
        esp_timer_create(&args, &t);
    }
    esp_timer_start_once(t, 1000000);
    return ESP_OK;
}

// ---------------------------------------------------------------------------
// Device log: recent console output (see WebLog.h), live
// ---------------------------------------------------------------------------

static const char *LOGS_HTML = R"html(<!DOCTYPE html>
<html>
<head>
  <meta charset="utf-8">
  <meta name="viewport" content="width=device-width,initial-scale=1">
  <title>Device Log</title>
  <style>
    body { font-family: sans-serif; max-width: 1000px; margin: 24px auto; padding: 0 16px; }
    h1   { font-size: 1.3em; }
    .row { display: flex; gap: 12px; align-items: center; flex-wrap: wrap; margin: 8px 0; }
    pre  { background: #111; color: #ddd; padding: 12px; border-radius: 8px; height: 70vh; overflow: auto;
           font: 12px/1.35 ui-monospace, Menlo, Consolas, monospace; white-space: pre-wrap; word-break: break-all; }
    button { padding: 6px 12px; cursor: pointer; border-radius: 4px; border: 1px solid #aaa; }
    .muted { color: #777; font-size: 0.9em; }
  </style>
</head>
<body>
  <p style="margin:0 0 8px"><a href="/">&larr; Home</a></p>
  <h1>Device Log</h1>
  <div class="row">
    <label><input type="checkbox" id="follow" checked> follow</label>
    <label><input type="checkbox" id="pause"> pause</label>
    <button onclick="clearLog()">Clear</button>
    <button onclick="copyLog()">Copy</button>
    <span class="muted">Last 8 KB of console output, updated every second.</span>
  </div>
  <pre id="log"></pre>
  <script>
    let pos = 0;
    const el = document.getElementById('log');
    function poll() {
      if (document.getElementById('pause').checked) return;
      fetch('/api/logs?since=' + pos).then(r => {
        const next = parseInt(r.headers.get('X-Next') || pos);
        return r.text().then(t => {
          if (next < pos) el.textContent = '';          // device restarted or log cleared
          pos = next;
          if (t) {
            el.textContent += t;
            if (el.textContent.length > 200000) el.textContent = el.textContent.slice(-150000);
            if (document.getElementById('follow').checked) el.scrollTop = el.scrollHeight;
          }
        });
      }).catch(() => {});
    }
    function clearLog() { fetch('/api/logs/clear', { method: 'POST' }).then(() => { el.textContent = ''; pos = 0; }); }
    function copyLog() { navigator.clipboard && navigator.clipboard.writeText(el.textContent); }
    poll();
    setInterval(poll, 1000);
  </script>
</body>
</html>)html";

esp_err_t WebBase::logs_page_handler(httpd_req_t *req)
{
    httpd_resp_set_type(req, "text/html");
    return httpd_resp_sendstr(req, LOGS_HTML);
}

esp_err_t WebBase::logs_get_handler(httpd_req_t *req)
{
    uint32_t since = 0;
    char     q[32], v[16];
    if (httpd_req_get_url_query_str(req, q, sizeof(q)) == ESP_OK && httpd_query_key_value(q, "since", v, sizeof(v)) == ESP_OK)
        since = (uint32_t)strtoul(v, nullptr, 10);

    char *buf = (char *)malloc(weblog::BUF_SIZE + 1);
    if (!buf)
        return httpd_resp_send_500(req);
    size_t   n    = 0;
    uint32_t next = weblog::read(since, buf, weblog::BUF_SIZE + 1, &n);

    char next_s[12];
    snprintf(next_s, sizeof(next_s), "%lu", (unsigned long)next);
    httpd_resp_set_type(req, "text/plain; charset=utf-8");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    httpd_resp_set_hdr(req, "X-Next", next_s);
    esp_err_t ret = httpd_resp_send(req, buf, (ssize_t)n);
    free(buf);
    return ret;
}

esp_err_t WebBase::logs_clear_handler(httpd_req_t *req)
{
    weblog::clear();
    return httpd_resp_sendstr(req, "OK");
}
