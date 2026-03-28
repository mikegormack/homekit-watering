
#include "ControlServer.h"

#include <esp_log.h>
#include <cJSON.h>
#include <string.h>
#include <stdio.h>
#include <iocfg.h>

static const char *TAG = "ControlServer";

// ---------------------------------------------------------------------------
// HTML page
// ---------------------------------------------------------------------------

static const char *CONTROL_HTML = R"html(<!DOCTYPE html>
<html>
<head>
  <meta charset="utf-8">
  <meta name="viewport" content="width=device-width,initial-scale=1">
  <title>Sprinkler Control</title>
  <style>
    body  { font-family: sans-serif; max-width: 600px; margin: 40px auto; padding: 0 16px; }
    h1    { font-size: 1.4em; }
    h2    { font-size: 1.1em; border-bottom: 1px solid #ccc; padding-bottom: 4px; margin-top: 0; }
    .card { border: 1px solid #ddd; border-radius: 8px; padding: 16px; margin-bottom: 16px; }
    .row  { display: flex; align-items: center; gap: 12px; margin: 8px 0; flex-wrap: wrap; }
    .lbl  { min-width: 40px; font-weight: bold; }
    .on   { color: #1a8a3a; font-weight: bold; }
    .off  { color: #888; }
    button { padding: 7px 14px; cursor: pointer; border-radius: 4px; border: 1px solid #aaa; }
    button.stop { background: #fee; border-color: #c66; }
    input[type=number] { width: 56px; padding: 4px; }
    input[type=time]   { padding: 4px; }
    table  { border-collapse: collapse; width: 100%; }
    td, th { padding: 6px 10px; text-align: left; }
    th     { font-weight: normal; color: #666; font-size: 0.9em; }
    #msg   { margin-top: 8px; font-size: 0.9em; }
  </style>
</head>
<body>
  <h1>Sprinkler Control</h1>

  <div class="card">
    <h2>Valve Status</h2>
    <div class="row">
      <span class="lbl">CH1</span>
      <span id="ch0-state" class="off">--</span>
      <span id="ch0-rem"></span>
      <button onclick="valveRun(0)">Run 20 min</button>
      <button class="stop" onclick="valveStop(0)">Stop</button>
    </div>
    <div class="row">
      <span class="lbl">CH2</span>
      <span id="ch1-state" class="off">--</span>
      <span id="ch1-rem"></span>
      <button onclick="valveRun(1)">Run 20 min</button>
      <button class="stop" onclick="valveStop(1)">Stop</button>
    </div>
    <div class="row">
      <span class="lbl">Soil</span>
      <span id="moisture">--</span>%
    </div>
  </div>

  <div class="card">
    <h2>Schedule</h2>
    <table>
      <tr><th></th><th>Event 1 time</th><th>Dur (min)</th><th>Event 2 time</th><th>Dur (min)</th></tr>
      <tr>
        <td><b>CH1</b></td>
        <td><input type="time" id="c0e0t"></td>
        <td><input type="number" id="c0e0d" min="0" max="255"></td>
        <td><input type="time" id="c0e1t"></td>
        <td><input type="number" id="c0e1d" min="0" max="255"></td>
      </tr>
      <tr>
        <td><b>CH2</b></td>
        <td><input type="time" id="c1e0t"></td>
        <td><input type="number" id="c1e0d" min="0" max="255"></td>
        <td><input type="time" id="c1e1t"></td>
        <td><input type="number" id="c1e1d" min="0" max="255"></td>
      </tr>
    </table>
    <div class="row" style="margin-top:12px">
      <button onclick="saveSchedule()">Save Schedule</button>
      <span id="msg"></span>
    </div>
  </div>

  <script>
    function pad(n) { return String(n).padStart(2,'0'); }

    function fmtRemaining(s) {
      if (s <= 0) return '';
      const m = Math.floor(s / 60), sec = s % 60;
      return '(' + pad(m) + ':' + pad(sec) + ' remaining)';
    }

    function refreshStatus() {
      fetch('/api/status').then(r => r.json()).then(d => {
        for (let ch = 0; ch < 2; ch++) {
          const v = d.valves[ch];
          const stEl = document.getElementById('ch' + ch + '-state');
          const remEl = document.getElementById('ch' + ch + '-rem');
          if (v.active) {
            stEl.textContent = 'ON';
            stEl.className = 'on';
          } else {
            stEl.textContent = 'OFF';
            stEl.className = 'off';
          }
          remEl.textContent = fmtRemaining(v.remaining);
        }
        document.getElementById('moisture').textContent = d.moisture;
      }).catch(() => {});
    }

    function loadSchedule() {
      fetch('/api/schedule').then(r => r.json()).then(d => {
        for (let ch = 0; ch < 2; ch++) {
          for (let e = 0; e < 2; e++) {
            const ev = d.channels[ch].events[e];
            document.getElementById('c' + ch + 'e' + e + 't').value = pad(ev.hour) + ':' + pad(ev.min);
            document.getElementById('c' + ch + 'e' + e + 'd').value = ev.dur;
          }
        }
      }).catch(() => {});
    }

    function saveSchedule() {
      const channels = [];
      for (let ch = 0; ch < 2; ch++) {
        const events = [];
        for (let e = 0; e < 2; e++) {
          const t = document.getElementById('c' + ch + 'e' + e + 't').value.split(':');
          const d = parseInt(document.getElementById('c' + ch + 'e' + e + 'd').value) || 0;
          events.push({ hour: parseInt(t[0])||0, min: parseInt(t[1])||0, dur: d });
        }
        channels.push({ events });
      }
      fetch('/api/schedule', {
        method: 'POST',
        headers: { 'Content-Type': 'application/json' },
        body: JSON.stringify({ channels })
      }).then(r => r.text()).then(t => {
        document.getElementById('msg').textContent = t;
        setTimeout(() => document.getElementById('msg').textContent = '', 3000);
      });
    }

    function valveRun(ch) {
      fetch('/api/valve/' + ch + '/run', { method: 'POST' });
    }

    function valveStop(ch) {
      fetch('/api/valve/' + ch + '/stop', { method: 'POST' });
    }

    loadSchedule();
    refreshStatus();
    setInterval(refreshStatus, 2000);
  </script>
</body>
</html>)html";

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

static ControlServer *server_from_req(httpd_req_t *req)
{
    return reinterpret_cast<ControlServer *>(req->user_ctx);
}

// ---------------------------------------------------------------------------
// Server lifecycle
// ---------------------------------------------------------------------------

void ControlServer::start(httpd_handle_t server)
{
    m_server = server;

    for (int i = 0; i < NUM_VALVES; i++)
        m_valve_ctx[i] = {this, i};

    httpd_uri_t fixed_uris[] = {
        { "/control",      HTTP_GET,  index_handler,     this },
        { "/api/status",   HTTP_GET,  status_handler,    this },
        { "/api/schedule", HTTP_GET,  sched_get_handler, this },
        { "/api/schedule", HTTP_POST, sched_post_handler,this },
    };
    for (auto &u : fixed_uris)
        httpd_register_uri_handler(m_server, &u);

    for (int i = 0; i < NUM_VALVES; i++)
    {
        snprintf(m_run_uri[i],  sizeof(m_run_uri[i]),  "/api/valve/%d/run",  i);
        snprintf(m_stop_uri[i], sizeof(m_stop_uri[i]), "/api/valve/%d/stop", i);
        httpd_uri_t u = { m_run_uri[i],  HTTP_POST, valve_run_handler,  &m_valve_ctx[i] };
        httpd_register_uri_handler(m_server, &u);
        u = { m_stop_uri[i], HTTP_POST, valve_stop_handler, &m_valve_ctx[i] };
        httpd_register_uri_handler(m_server, &u);
    }

    ESP_LOGI(TAG, "Control server started on port 8081");
}

void ControlServer::stop()
{
    m_server = nullptr;
}

// ---------------------------------------------------------------------------
// Handlers
// ---------------------------------------------------------------------------

esp_err_t ControlServer::index_handler(httpd_req_t *req)
{
    httpd_resp_set_type(req, "text/html");
    httpd_resp_sendstr(req, CONTROL_HTML);
    return ESP_OK;
}

esp_err_t ControlServer::status_handler(httpd_req_t *req)
{
    ControlServer *s = server_from_req(req);

    char buf[192];
    int  n = 0;
    n += snprintf(buf + n, sizeof(buf) - n, "{\"valves\":[");
    for (int ch = 0; ch < NUM_VALVES; ch++)
    {
        ValveStatus v = s->valve_status ? s->valve_status(ch) : ValveStatus{false, 0};
        n += snprintf(buf + n, sizeof(buf) - n,
            "%s{\"active\":%s,\"remaining\":%lu}",
            ch ? "," : "",
            v.active ? "true" : "false",
            (unsigned long)v.remaining_s);
    }
    uint8_t mo = s->moisture ? s->moisture() : 0;
    snprintf(buf + n, sizeof(buf) - n, "],\"moisture\":%d}", (int)mo);

    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(req, buf);
    return ESP_OK;
}

esp_err_t ControlServer::valve_run_handler(httpd_req_t *req)
{
    const ValveCtx *ctx = reinterpret_cast<const ValveCtx *>(req->user_ctx);
    if (ctx->server->valve_run) ctx->server->valve_run(ctx->ch, DEFAULT_RUN_DURATION_S);
    httpd_resp_sendstr(req, "OK");
    return ESP_OK;
}

esp_err_t ControlServer::valve_stop_handler(httpd_req_t *req)
{
    const ValveCtx *ctx = reinterpret_cast<const ValveCtx *>(req->user_ctx);
    if (ctx->server->valve_stop) ctx->server->valve_stop(ctx->ch);
    httpd_resp_sendstr(req, "OK");
    return ESP_OK;
}

esp_err_t ControlServer::sched_get_handler(httpd_req_t *req)
{
    ControlServer *s = server_from_req(req);

    char buf[256];
    int  n = 0;
    n += snprintf(buf + n, sizeof(buf) - n, "{\"channels\":[");
    for (int ch = 0; ch < NUM_VALVES; ch++)
    {
        const time_evt_t *evts = s->sched_get ? s->sched_get(ch) : nullptr;
        n += snprintf(buf + n, sizeof(buf) - n, "%s{\"events\":[", ch ? "," : "");
        for (int e = 0; e < EVT_PER_CH; e++)
        {
            uint8_t h = evts ? evts[e].hour     : 0;
            uint8_t m = evts ? evts[e].min      : 0;
            uint8_t d = evts ? evts[e].duration : 0;
            n += snprintf(buf + n, sizeof(buf) - n,
                "%s{\"hour\":%d,\"min\":%d,\"dur\":%d}",
                e ? "," : "", h, m, d);
        }
        n += snprintf(buf + n, sizeof(buf) - n, "]}");
    }
    snprintf(buf + n, sizeof(buf) - n, "]}");

    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(req, buf);
    return ESP_OK;
}

esp_err_t ControlServer::sched_post_handler(httpd_req_t *req)
{
    ControlServer *s = server_from_req(req);

    // Read body
    char body[512];
    int len = httpd_req_recv(req, body, sizeof(body) - 1);
    if (len <= 0)
    {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Empty body");
        return ESP_FAIL;
    }
    body[len] = '\0';

    // Parse JSON: {"channels":[{"events":[{"hour":H,"min":M,"dur":D},...]},...]}
    cJSON *root = cJSON_Parse(body);
    if (!root)
    {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid JSON");
        return ESP_FAIL;
    }

    cJSON *channels = cJSON_GetObjectItem(root, "channels");
    if (!cJSON_IsArray(channels) || cJSON_GetArraySize(channels) < NUM_VALVES)
    {
        cJSON_Delete(root);
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Bad schema");
        return ESP_FAIL;
    }

    for (int ch = 0; ch < NUM_VALVES; ch++)
    {
        cJSON *chan   = cJSON_GetArrayItem(channels, ch);
        cJSON *events = cJSON_GetObjectItem(chan, "events");
        if (!cJSON_IsArray(events) || cJSON_GetArraySize(events) < EVT_PER_CH)
            continue;

        time_evt_t evts[EVT_PER_CH] = {};
        for (int e = 0; e < EVT_PER_CH; e++)
        {
            cJSON *ev = cJSON_GetArrayItem(events, e);
            if (!cJSON_IsObject(ev)) continue;
            cJSON *h = cJSON_GetObjectItem(ev, "hour");
            cJSON *m = cJSON_GetObjectItem(ev, "min");
            cJSON *d = cJSON_GetObjectItem(ev, "dur");
            if (!cJSON_IsNumber(h) || !cJSON_IsNumber(m) || !cJSON_IsNumber(d)) continue;
            evts[e].hour     = (uint8_t)cJSON_GetNumberValue(h);
            evts[e].min      = (uint8_t)cJSON_GetNumberValue(m);
            evts[e].duration = (uint8_t)cJSON_GetNumberValue(d);
        }

        if (s->sched_set) s->sched_set(ch, evts);
    }

    cJSON_Delete(root);
    httpd_resp_sendstr(req, "Schedule saved");
    return ESP_OK;
}
