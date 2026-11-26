#include "ProbeServer.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#include <cJSON.h>
#include <esp_log.h>
#include <esp_system.h>
#include <esp_timer.h>

static const char *TAG = "ProbeServer";

using namespace probe_reg;

// ---------------------------------------------------------------------------
// HTML page
// ---------------------------------------------------------------------------

static const char *PROBES_HTML = R"html(<!DOCTYPE html>
<html>
<head>
  <meta charset="utf-8">
  <meta name="viewport" content="width=device-width,initial-scale=1">
  <title>Soil Probes</title>
  <style>
    body  { font-family: sans-serif; max-width: 760px; margin: 24px auto 40px; padding: 0 16px; }
    .nav  { margin: 0 0 8px; }
    h1    { font-size: 1.4em; }
    h2    { font-size: 1.1em; border-bottom: 1px solid #ccc; padding-bottom: 4px; margin-top: 0; }
    h3    { font-size: 1em; margin: 16px 0 6px; }
    .card { border: 1px solid #ddd; border-radius: 8px; padding: 16px; margin-bottom: 16px; }
    .row  { display: flex; align-items: center; gap: 10px; margin: 8px 0; flex-wrap: wrap; }
    .muted { color: #888; font-size: 0.9em; }
    .ok   { color: #1a8a3a; }
    .bad  { color: #c33; }
    .warn { color: #b77400; }
    button { padding: 6px 12px; cursor: pointer; border-radius: 4px; border: 1px solid #aaa; background: #f8f8f8; }
    button.stop { background: #fee; border-color: #c66; }
    button:disabled { opacity: 0.5; cursor: default; }
    input[type=number] { width: 70px; padding: 4px; }
    input[type=text]   { width: 120px; padding: 4px; }
    select { padding: 4px; }
    table  { border-collapse: collapse; width: 100%; }
    td, th { padding: 5px 8px; text-align: left; }
    th     { font-weight: normal; color: #666; font-size: 0.9em; }
    tr.sel { background: #eef5ff; }
    tbody tr { cursor: pointer; }
    .kv td:first-child { color: #666; width: 45%; }
    progress { width: 100%; }
    #msg { min-height: 1.2em; font-size: 0.9em; }
    a { color: #2a5db0; }
  </style>
</head>
<body>
  <p class="nav"><a href="/">&larr; Home</a></p>
  <h1>Soil Probes</h1>
  <div id="msg"></div>

  <div class="card">
    <h2>Probes</h2>
    <table>
      <thead><tr><th>Addr</th><th>Name</th><th>Moisture</th><th>Temp</th><th>Status</th><th>Firmware</th></tr></thead>
      <tbody id="plist"><tr><td colspan="6" class="muted">Loading...</td></tr></tbody>
    </table>
    <p class="muted">Click a probe for details, calibration and firmware update.</p>
  </div>

  <div class="card" id="detail" style="display:none">
    <h2 id="d-title">Probe</h2>
    <table class="kv"><tbody id="d-live"></tbody></table>

    <h3>Calibration</h3>
    <p class="muted">1. Probe in dry soil (or air for a first test), wait for a few readings, <b>Capture dry</b>.
      2. Probe in saturated soil, <b>Capture wet</b>. 3. <b>Save to probe</b> (otherwise lost at power-off).</p>
    <div class="row">
      <button onclick="cmd(3)">Capture dry</button>
      <button onclick="cmd(4)">Capture wet</button>
      <button onclick="cmd(1)">Save to probe</button>
    </div>
    <table class="kv">
      <tr><td>Dry ratio (0 %)</td><td><input type="number" id="s-dry" step="0.0001"></td></tr>
      <tr><td>Wet ratio (100 %)</td><td><input type="number" id="s-wet" step="0.0001"></td></tr>
      <tr><td>Temperature compensation</td><td><input type="checkbox" id="s-tcen"> on</td></tr>
      <tr><td>Compensation coefficient (% per &deg;C)</td><td><input type="number" id="s-tcc" step="0.01"></td></tr>
      <tr><td>Compensation reference (&deg;C)</td><td><input type="number" id="s-tcr" step="0.1"></td></tr>
      <tr><td>Excitation (16 MHz &gt;&gt; n)</td><td><input type="number" id="s-mco" min="0" max="7"></td></tr>
      <tr><td>Excitation settle (ms)</td><td><input type="number" id="s-settle" min="1" max="1000"></td></tr>
      <tr><td>Measurement interval (ms)</td><td><input type="number" id="s-int" min="100" max="60000"></td></tr>
    </table>
    <div class="row">
      <button onclick="loadSettings()">Reload</button>
      <button onclick="writeSettings()">Write &amp; save</button>
      <span id="s-bus" class="muted"></span>
    </div>

    <h3>Probe</h3>
    <div class="row">
      <input type="text" id="d-name" maxlength="15"> <button onclick="rename()">Rename</button>
    </div>
    <div class="row">
      New address <input type="number" id="d-addr" min="1" max="247"> <button onclick="readdress()">Change address</button>
    </div>
    <div class="row">
      <button onclick="cmd(2)">Reboot probe</button>
      <button onclick="if(confirm('Reset probe settings (calibration) to defaults? Address and baud are kept.')) cmd(5)">Defaults</button>
      <button class="stop" onclick="removeProbe()">Remove from list</button>
    </div>

    <h3>Firmware update</h3>
    <p class="muted">Upload <code>build/firmware/soil_probe.bin</code> from the soil-moisture-probe build (about
      30 s at 9600 baud). Probes on firmware 1.0 have no bootloader and need one SWD flash first.</p>
    <div class="row">
      <input type="file" id="fw-file" accept=".bin">
      <button id="fw-btn" onclick="uploadFw()">Upload &amp; update</button>
      <button id="fw-again" onclick="updateStaged()" style="display:none">Update with staged image</button>
    </div>
  </div>

  <div class="card" id="jobcard" style="display:none">
    <h2 id="job-title">Job</h2>
    <progress id="job-prog" max="100" value="0"></progress>
    <div id="job-msg" class="muted"></div>
    <table><tbody id="scan-hits"></tbody></table>
  </div>

  <div class="card">
    <h2>Zones</h2>
    <p class="muted">A scheduled run is skipped while the zone's probe reads at or above the threshold.
      Without a usable reading (offline, uncalibrated) the zone is watered as scheduled.</p>
    <table>
      <tr><th></th><th>Probe</th><th>Skip at (%)</th><th>Now</th></tr>
      <tr><td><b>CH1</b></td><td><select id="z0p"></select></td><td><input type="number" id="z0t" min="0" max="100"></td><td id="z0m"></td></tr>
      <tr><td><b>CH2</b></td><td><select id="z1p"></select></td><td><input type="number" id="z1t" min="0" max="100"></td><td id="z1m"></td></tr>
    </table>
    <div class="row"><button onclick="saveZones()">Save zones</button></div>
  </div>

  <div class="card">
    <h2>Add probes</h2>
    <p class="muted">New probes answer on address 1. Connect one new probe at a time, add it, then change its
      address so the next new one can use 1. Each probe appears in the Home app as a humidity and a temperature sensor once the controller
      has restarted (<a href="/system">System</a> page).</p>
    <div class="row">
      Address <input type="number" id="a-addr" min="1" max="247" value="1">
      Name <input type="text" id="a-name" maxlength="15" placeholder="e.g. Bed A">
      <button onclick="addProbe()">Add</button>
    </div>
    <div class="row">
      <button id="scan-btn" onclick="scan()">Scan bus (1-247)</button>
      <span class="muted">takes about 30 s</span>
    </div>
  </div>

  <script>
    let state = null, sel = 0, zonesLoaded = false, scanShown = false;
    const $ = id => document.getElementById(id);

    function say(t, bad) {
      $('msg').textContent = t; $('msg').className = bad ? 'bad' : 'ok';
      clearTimeout(say.t); say.t = setTimeout(() => $('msg').textContent = '', 6000);
    }
    function post(body) {
      return fetch('/api/probes', { method: 'POST', headers: { 'Content-Type': 'application/json' },
        body: JSON.stringify(body) }).then(r => r.json()).then(d => {
          if (!d.ok) throw new Error(d.error || 'failed');
          return d;
        });
    }
    function act(body, okText) {
      return post(body).then(d => { if (okText) say(okText); refresh(); return d; })
        .catch(e => { say(e.message, true); throw e; });
    }
    function ver(v) { return v ? (v >> 8) + '.' + (v & 0xff) : '-'; }
    function blver(v) { return v === null ? '?' : '0x' + v.toString(16).toUpperCase(); }
    function esc(s) { return String(s).replace(/[&<>"]/g, c => ({'&':'&amp;','<':'&lt;','>':'&gt;','"':'&quot;'}[c])); }

    function status(p) {
      if (!p.valid && !p.online) return '<span class="bad">no reply</span>';
      if (!p.online) return '<span class="bad">offline</span>';
      if (p.in_bl) return '<span class="warn">bootloader' + (p.bl_status & 0x8000 ? ' (update failed)' :
        p.bl_status === 1 ? ' (receiving)' : '') + '</span>';
      const f = [];
      if (p.uncal) f.push('not calibrated');
      if (p.ntc_fault) f.push('NTC fault');
      if (p.ref_low) f.push('REF low');
      if (p.cfg_defaults) f.push('defaults');
      if (p.save_failed) f.push('save failed');
      return f.length ? '<span class="warn">' + f.join(', ') + '</span>' : '<span class="ok">ok</span>';
    }

    function render() {
      const tb = $('plist');
      tb.innerHTML = '';
      if (!state.probes.length)
        tb.innerHTML = '<tr><td colspan="6" class="muted">No probes yet - add one below.</td></tr>';
      state.probes.forEach(p => {
        const tr = document.createElement('tr');
        if (p.addr === sel) tr.className = 'sel';
        const live = p.online && !p.in_bl;
        tr.innerHTML = '<td>' + p.addr + '</td><td>' + esc(p.name) + '</td><td>' +
          (live && !p.uncal ? p.moisture.toFixed(1) + ' %' : '-') + '</td><td>' +
          (live && !p.ntc_fault ? p.temp.toFixed(1) + ' &deg;C' : '-') + '</td><td>' + status(p) + '</td><td>' +
          ver(p.fw) + (p.has_bl ? '' : ' <span class="muted">(no bootloader)</span>') + '</td>';
        tr.onclick = () => select(p.addr);
        tb.appendChild(tr);
      });

      // Zone selects: rebuilt only when the probe list changes, keeping what the user picked.
      const key = state.probes.map(p => p.addr + ':' + p.name).join('|');
      for (let ch = 0; ch < 2; ch++) {
        const z = state.zones[ch], s = $('z' + ch + 'p');
        if (s.dataset.key !== key) {
          const cur = zonesLoaded ? s.value : String(z.probe);
          s.innerHTML = '<option value="0">none</option>' + state.probes.map(p =>
            '<option value="' + p.addr + '">' + p.addr + ' ' + esc(p.name) + '</option>').join('');
          if (z.probe && !state.probes.some(p => p.addr === z.probe))
            s.innerHTML += '<option value="' + z.probe + '">' + z.probe + ' (not in list)</option>';
          s.value = cur;
          if (s.value === '') s.value = String(z.probe);
          s.dataset.key = key;
        }
        if (!zonesLoaded) $('z' + ch + 't').value = z.thr;
        $('z' + ch + 'm').textContent = z.moisture === null ? '-' : z.moisture.toFixed(1) + ' %';
      }
      zonesLoaded = true;

      const p = state.probes.find(q => q.addr === sel);
      $('detail').style.display = p ? '' : 'none';
      if (p) {
        $('d-title').textContent = 'Probe ' + p.addr + ' - ' + p.name;
        const rows = [
          ['Moisture (compensated / raw)', p.uncal ? 'not calibrated' : p.moisture.toFixed(2) + ' % / ' + p.moisture_raw.toFixed(2) + ' %'],
          ['Temperature', p.ntc_fault ? 'NTC fault' : p.temp.toFixed(2) + ' °C'],
          ['Ratio SENSE/REF', p.ratio.toFixed(4)],
          ['SENSE / REF / NTC raw', p.sense + ' / ' + p.ref + ' / ' + p.ntc],
          ['VDDA', p.vdda + ' mV'],
          ['Firmware / bootloader', ver(p.fw) + ' / ' + (p.has_bl ? (p.bl === null ? 'yes' : blver(p.bl)) : 'none')],
          ['Measurements / failed polls', p.count + ' / ' + p.fails],
          ['Last reply', p.age_s === null ? 'never' : p.age_s + ' s ago'],
        ];
        $('d-live').innerHTML = rows.map(r => '<tr><td>' + r[0] + '</td><td>' + r[1] + '</td></tr>').join('');
      }

      const img = state.image;
      $('fw-again').style.display = img.ready ? '' : 'none';
      if (img.ready) $('fw-again').textContent = 'Update with staged image (' + img.size + ' bytes)';

      const j = state.job;
      const busy = j.state === 'running';
      ['fw-btn', 'fw-again', 'scan-btn'].forEach(id => $(id).disabled = busy);
      if (j.type !== 'none') {
        $('jobcard').style.display = '';
        $('job-title').textContent = j.type === 'scan' ? 'Bus scan' : 'Firmware update - probe ' + j.addr;
        $('job-prog').value = j.total ? Math.round(j.done * 100 / j.total) : 0;
        $('job-msg').textContent = j.msg + (j.state === 'failed' ? ' (failed)' : '');
        $('job-msg').className = j.state === 'failed' ? 'bad' : j.state === 'done' ? 'ok' : 'muted';
        const hb = $('scan-hits');
        if (j.type === 'scan') {
          hb.innerHTML = j.hits.map(h => {
            const known = state.probes.some(p => p.addr === h.addr);
            return '<tr><td>Address ' + h.addr + '</td><td>' + (h.fw ? 'firmware ' + ver(h.fw) : 'not a probe?') +
              (h.bl ? ' (bootloader)' : '') + '</td><td>' + (known ? '<span class="muted">in list</span>' :
              '<button onclick="addAddr(' + h.addr + ')">Add</button>') + '</td></tr>';
          }).join('');
        } else hb.innerHTML = '';
      }
    }

    function refresh() {
      return fetch('/api/probes').then(r => r.json()).then(d => { state = d; render(); }).catch(() => {});
    }

    function select(a) {
      sel = a;
      const p = state.probes.find(q => q.addr === a);
      $('d-name').value = p ? p.name : '';
      $('d-addr').value = '';
      render();
      loadSettings();
    }

    function loadSettings() {
      if (!sel) return;
      $('s-bus').textContent = 'reading...';
      fetch('/api/probe?addr=' + sel).then(r => r.json()).then(d => {
        if (!d.ok) throw new Error(d.error);
        const s = d.settings;
        $('s-dry').value = (s.cal_dry / 10000).toFixed(4);
        $('s-wet').value = (s.cal_wet / 10000).toFixed(4);
        $('s-tcen').checked = !!(s.flags & 1);
        $('s-tcc').value = (s.tc_coef / 100).toFixed(2);
        $('s-tcr').value = (s.tc_ref / 100).toFixed(1);
        $('s-mco').value = s.mco_div;
        $('s-settle').value = s.settle_ms;
        $('s-int').value = s.interval_ms;
        $('s-bus').textContent = 'address ' + s.addr + ', ' + (s.baud * 100) + ' baud, parity ' + ['none', 'even', 'odd'][s.parity];
      }).catch(e => $('s-bus').textContent = 'settings: ' + e.message);
    }

    function writeSettings() {
      const n = id => parseFloat($(id).value);
      act({ action: 'cal', addr: sel, save: true,
        cal_dry: Math.round(n('s-dry') * 10000), cal_wet: Math.round(n('s-wet') * 10000),
        tc_en: $('s-tcen').checked, tc_coef: Math.round(n('s-tcc') * 100), tc_ref: Math.round(n('s-tcr') * 100),
        mco_div: n('s-mco'), settle_ms: n('s-settle'), interval_ms: n('s-int') }, 'Settings written and saved')
        .then(loadSettings).catch(() => {});
    }

    function cmd(c) {
      const names = { 1: 'Saved', 2: 'Rebooting probe', 3: 'Dry point captured - save to keep it',
                      4: 'Wet point captured - save to keep it', 5: 'Defaults restored - save to keep them' };
      act({ action: 'cmd', addr: sel, cmd: c }, names[c]).then(() => setTimeout(loadSettings, 500)).catch(() => {});
    }
    function rename() { act({ action: 'rename', addr: sel, name: $('d-name').value }, 'Renamed').catch(() => {}); }
    function readdress() {
      const a = parseInt($('d-addr').value);
      if (!(a >= 1 && a <= 247)) return say('Address must be 1-247', true);
      say('Changing address...');
      act({ action: 'address', addr: sel, new_addr: a }, 'Probe moved to address ' + a).then(() => select(a)).catch(() => {});
    }
    function removeProbe() {
      if (!confirm('Remove probe ' + sel + ' from the list? Zones using it lose their probe.')) return;
      act({ action: 'remove', addr: sel }, 'Removed').then(() => { sel = 0; render(); }).catch(() => {});
    }
    function addAddr(a) { $('a-addr').value = a; addProbe(); }
    function addProbe() {
      const a = parseInt($('a-addr').value);
      act({ action: 'add', addr: a, name: $('a-name').value || ('Probe ' + a) }, 'Added probe ' + a)
        .then(() => select(a)).catch(() => {});
    }
    function scan() { act({ action: 'scan' }, 'Scanning...').catch(() => {}); }
    function saveZones() {
      const zones = [0, 1].map(ch => ({ probe: parseInt($('z' + ch + 'p').value) || 0,
                                       thr: parseInt($('z' + ch + 't').value) || 0 }));
      act({ action: 'zones', zones }, 'Zones saved').catch(() => {});
    }
    function updateStaged() {
      if (!confirm('Update probe ' + sel + ' with the staged image?')) return;
      act({ action: 'update', addr: sel }, 'Update started').catch(() => {});
    }
    function uploadFw() {
      const f = $('fw-file').files[0];
      if (!f) return say('Choose a .bin file first', true);
      if (!confirm('Update probe ' + sel + ' with ' + f.name + '?')) return;
      say('Uploading...');
      fetch('/api/probe/fw?addr=' + sel, { method: 'POST', headers: { 'Content-Type': 'application/octet-stream' }, body: f })
        .then(r => r.json()).then(d => { if (!d.ok) throw new Error(d.error); say('Image staged, update started'); refresh(); })
        .catch(e => say(e.message, true));
    }

    refresh();
    setInterval(refresh, 2000);
  </script>
</body>
</html>)html";

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

static ProbeServer *self_of(httpd_req_t *req) { return static_cast<ProbeServer *>(req->user_ctx); }

static esp_err_t send_json(httpd_req_t *req, cJSON *root)
{
    char *s = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    esp_err_t ret = s ? httpd_resp_sendstr(req, s) : httpd_resp_send_500(req);
    free(s);
    return ret;
}

static esp_err_t send_result(httpd_req_t *req, int err)
{
    cJSON *root = cJSON_CreateObject();
    cJSON_AddBoolToObject(root, "ok", err == 0);
    if (err)
        cJSON_AddStringToObject(root, "error", ProbeManager::errStr(err));
    return send_json(req, root);
}

static esp_err_t send_error(httpd_req_t *req, const char *msg)
{
    cJSON *root = cJSON_CreateObject();
    cJSON_AddBoolToObject(root, "ok", false);
    cJSON_AddStringToObject(root, "error", msg);
    return send_json(req, root);
}

static int query_int(httpd_req_t *req, const char *key, int def)
{
    char q[64], v[16];
    if (httpd_req_get_url_query_str(req, q, sizeof(q)) != ESP_OK)
        return def;
    if (httpd_query_key_value(q, key, v, sizeof(v)) != ESP_OK)
        return def;
    return atoi(v);
}

static bool get_num(cJSON *obj, const char *key, double &out)
{
    cJSON *it = cJSON_GetObjectItem(obj, key);
    if (!cJSON_IsNumber(it))
        return false;
    out = cJSON_GetNumberValue(it);
    return true;
}

static int get_int(cJSON *obj, const char *key, int def)
{
    double d;
    return get_num(obj, key, d) ? (int)lround(d) : def;
}

static const char *job_type_str(ProbeManager::JobType t)
{
    switch (t)
    {
        case ProbeManager::JobType::SCAN:   return "scan";
        case ProbeManager::JobType::UPDATE: return "update";
        default:                            return "none";
    }
}

static const char *job_state_str(ProbeManager::JobState s)
{
    switch (s)
    {
        case ProbeManager::JobState::RUNNING: return "running";
        case ProbeManager::JobState::DONE:    return "done";
        case ProbeManager::JobState::FAILED:  return "failed";
        default:                              return "idle";
    }
}

// ---------------------------------------------------------------------------
// Lifecycle
// ---------------------------------------------------------------------------

void ProbeServer::start(httpd_handle_t server)
{
    m_server = server;
    httpd_uri_t uris[] = {
        {"/probes",       HTTP_GET,  page_handler,     this},
        {"/api/probes",   HTTP_GET,  list_handler,     this},
        {"/api/probes",   HTTP_POST, action_handler,   this},
        {"/api/probe",    HTTP_GET,  settings_handler, this},
        {"/api/probe/fw", HTTP_POST, fw_handler,       this},
    };
    for (auto &u : uris)
        if (httpd_register_uri_handler(m_server, &u) != ESP_OK)
            ESP_LOGE(TAG, "Failed to register %s", u.uri);
    ESP_LOGI(TAG, "Probe handlers registered");
}

// ---------------------------------------------------------------------------
// Handlers
// ---------------------------------------------------------------------------

esp_err_t ProbeServer::page_handler(httpd_req_t *req)
{
    httpd_resp_set_type(req, "text/html");
    return httpd_resp_sendstr(req, PROBES_HTML);
}

esp_err_t ProbeServer::list_handler(httpd_req_t *req)
{
    ProbeServer *s   = self_of(req);
    int64_t      now = esp_timer_get_time();

    cJSON *root = cJSON_CreateObject();
    cJSON *arr  = cJSON_AddArrayToObject(root, "probes");
    for (const auto &p : s->m_probes.list())
    {
        const auto &r = p.r;
        cJSON      *o = cJSON_CreateObject();
        cJSON_AddNumberToObject(o, "addr", p.addr);
        cJSON_AddStringToObject(o, "name", p.name);
        cJSON_AddBoolToObject(o, "valid", r.valid);
        cJSON_AddBoolToObject(o, "online", r.online);
        cJSON_AddBoolToObject(o, "in_bl", r.in_bootloader);
        cJSON_AddNumberToObject(o, "bl_status", r.bl_status);
        cJSON_AddBoolToObject(o, "has_bl", r.in_bootloader || r.fw_version >= FW_FIRST_WITH_BOOTLOADER);
        if (r.valid)
            cJSON_AddNumberToObject(o, "age_s", (double)((now - r.last_ok_us) / 1000000));
        else
            cJSON_AddNullToObject(o, "age_s");
        cJSON_AddNumberToObject(o, "fails", r.fails);
        cJSON_AddNumberToObject(o, "moisture", r.moisture / 100.0);
        cJSON_AddNumberToObject(o, "moisture_raw", r.moisture_raw / 100.0);
        cJSON_AddNumberToObject(o, "temp", r.temp / 100.0);
        cJSON_AddNumberToObject(o, "ratio", r.ratio / 10000.0);
        cJSON_AddNumberToObject(o, "sense", r.sense);
        cJSON_AddNumberToObject(o, "ref", r.ref);
        cJSON_AddNumberToObject(o, "ntc", r.ntc);
        cJSON_AddNumberToObject(o, "vdda", r.vdda_mv);
        cJSON_AddNumberToObject(o, "count", r.count);
        cJSON_AddNumberToObject(o, "status", r.status);
        cJSON_AddBoolToObject(o, "uncal", r.status & ST_UNCALIBRATED);
        cJSON_AddBoolToObject(o, "ntc_fault", r.status & ST_NTC_FAULT);
        cJSON_AddBoolToObject(o, "ref_low", r.status & ST_REF_LOW);
        cJSON_AddBoolToObject(o, "cfg_defaults", r.status & ST_CFG_DEFAULTS);
        cJSON_AddBoolToObject(o, "save_failed", r.status & ST_SAVE_FAILED);
        cJSON_AddNumberToObject(o, "fw", r.fw_version);
        if (r.bl_version >= 0)
            cJSON_AddNumberToObject(o, "bl", r.bl_version);
        else
            cJSON_AddNullToObject(o, "bl");
        cJSON_AddItemToArray(arr, o);
    }

    cJSON *zones = cJSON_AddArrayToObject(root, "zones");
    for (int ch = 0; ch < NUM_ZONES; ch++)
    {
        Zone   z = s->zone_get ? s->zone_get(ch) : Zone{0, 50};
        cJSON *o = cJSON_CreateObject();
        cJSON_AddNumberToObject(o, "probe", z.probe_addr);
        cJSON_AddNumberToObject(o, "thr", z.threshold);
        float m = s->m_probes.moisture(z.probe_addr);
        if (m >= 0)
            cJSON_AddNumberToObject(o, "moisture", m);
        else
            cJSON_AddNullToObject(o, "moisture");
        cJSON_AddItemToArray(zones, o);
    }

    ProbeManager::Job job = s->m_probes.job();
    cJSON            *j   = cJSON_AddObjectToObject(root, "job");
    cJSON_AddStringToObject(j, "type", job_type_str(job.type));
    cJSON_AddStringToObject(j, "state", job_state_str(job.state));
    cJSON_AddNumberToObject(j, "addr", job.addr);
    cJSON_AddNumberToObject(j, "done", job.done);
    cJSON_AddNumberToObject(j, "total", job.total);
    cJSON_AddStringToObject(j, "msg", job.msg);
    cJSON *hits = cJSON_AddArrayToObject(j, "hits");
    for (const auto &h : job.hits)
    {
        cJSON *o = cJSON_CreateObject();
        cJSON_AddNumberToObject(o, "addr", h.addr);
        cJSON_AddNumberToObject(o, "fw", h.fw_version);
        cJSON_AddBoolToObject(o, "bl", h.in_bootloader);
        cJSON_AddItemToArray(hits, o);
    }

    uint32_t size = 0, crc = 0;
    bool     ready = s->m_probes.imageInfo(size, crc);
    cJSON   *img   = cJSON_AddObjectToObject(root, "image");
    cJSON_AddBoolToObject(img, "ready", ready);
    cJSON_AddNumberToObject(img, "size", ready ? size : 0);
    char crc_s[12];
    snprintf(crc_s, sizeof(crc_s), "%08lx", (unsigned long)crc);
    cJSON_AddStringToObject(img, "crc", ready ? crc_s : "");

    return send_json(req, root);
}

esp_err_t ProbeServer::settings_handler(httpd_req_t *req)
{
    ProbeServer *s    = self_of(req);
    int          addr = query_int(req, "addr", 0);
    if (addr < 1 || addr > 247)
        return send_error(req, "bad address");

    ProbeManager::Settings st;
    int                    e = s->m_probes.readSettings((uint8_t)addr, st);
    if (e)
        return send_result(req, e);

    cJSON *root = cJSON_CreateObject();
    cJSON_AddBoolToObject(root, "ok", true);
    cJSON *o = cJSON_AddObjectToObject(root, "settings");
    cJSON_AddNumberToObject(o, "addr", st.slave_addr);
    cJSON_AddNumberToObject(o, "baud", st.baud_100);
    cJSON_AddNumberToObject(o, "parity", st.parity);
    cJSON_AddNumberToObject(o, "cal_dry", st.cal_dry);
    cJSON_AddNumberToObject(o, "cal_wet", st.cal_wet);
    cJSON_AddNumberToObject(o, "tc_coef", st.tc_coef);
    cJSON_AddNumberToObject(o, "tc_ref", st.tc_ref);
    cJSON_AddNumberToObject(o, "flags", st.flags);
    cJSON_AddNumberToObject(o, "mco_div", st.mco_div);
    cJSON_AddNumberToObject(o, "settle_ms", st.settle_ms);
    cJSON_AddNumberToObject(o, "interval_ms", st.interval_ms);
    return send_json(req, root);
}

esp_err_t ProbeServer::action_handler(httpd_req_t *req)
{
    ProbeServer  *s = self_of(req);
    ProbeManager &pm = s->m_probes;

    char body[512];
    if (req->content_len <= 0 || req->content_len >= (int)sizeof(body))
        return send_error(req, "bad request");
    int got = 0;
    while (got < (int)req->content_len)
    {
        int n = httpd_req_recv(req, body + got, req->content_len - got);
        if (n <= 0)
            return send_error(req, "receive failed");
        got += n;
    }
    body[got] = '\0';

    cJSON *root = cJSON_Parse(body);
    if (!root)
        return send_error(req, "invalid JSON");

    cJSON      *act_item = cJSON_GetObjectItem(root, "action");
    const char *act      = cJSON_IsString(act_item) ? act_item->valuestring : "";
    int         addr     = get_int(root, "addr", 0);
    cJSON      *name     = cJSON_GetObjectItem(root, "name");
    const char *name_s   = cJSON_IsString(name) ? name->valuestring : "";
    int         e        = 0;

    if (!strcmp(act, "add"))
    {
        e = pm.add((uint8_t)addr, name_s);
    }
    else if (!strcmp(act, "remove"))
    {
        e = pm.remove((uint8_t)addr);
        if (e == 0 && s->zone_get && s->zone_set)
            for (int ch = 0; ch < NUM_ZONES; ch++)
            {
                Zone z = s->zone_get(ch);
                if (z.probe_addr == addr)
                {
                    z.probe_addr = 0;
                    s->zone_set(ch, z);
                }
            }
    }
    else if (!strcmp(act, "rename"))
    {
        e = pm.rename((uint8_t)addr, name_s);
    }
    else if (!strcmp(act, "scan"))
    {
        e = pm.startScan();
    }
    else if (!strcmp(act, "cmd"))
    {
        int c = get_int(root, "cmd", 0);
        e     = (c >= CMD_SAVE && c <= CMD_DEFAULTS) ? pm.command((uint8_t)addr, (uint16_t)c) : ModbusMaster::ERR_ARG;
    }
    else if (!strcmp(act, "cal"))
    {
        // Start from the probe's current settings so a partial request only changes what it names.
        ProbeManager::Settings st;
        e = pm.readSettings((uint8_t)addr, st);
        if (e == 0)
        {
            st.cal_dry     = (uint16_t)get_int(root, "cal_dry", st.cal_dry);
            st.cal_wet     = (uint16_t)get_int(root, "cal_wet", st.cal_wet);
            st.tc_coef     = (int16_t)get_int(root, "tc_coef", st.tc_coef);
            st.tc_ref      = (int16_t)get_int(root, "tc_ref", st.tc_ref);
            st.mco_div     = (uint16_t)get_int(root, "mco_div", st.mco_div);
            st.settle_ms   = (uint16_t)get_int(root, "settle_ms", st.settle_ms);
            st.interval_ms = (uint16_t)get_int(root, "interval_ms", st.interval_ms);
            cJSON *tc_en   = cJSON_GetObjectItem(root, "tc_en");
            if (cJSON_IsBool(tc_en))
                st.flags = cJSON_IsTrue(tc_en) ? (st.flags | FLAG_TEMP_COMP) : (st.flags & ~FLAG_TEMP_COMP);
            cJSON *save = cJSON_GetObjectItem(root, "save");
            e           = pm.writeCalibration((uint8_t)addr, st, !cJSON_IsFalse(save));
        }
    }
    else if (!strcmp(act, "address"))
    {
        e = pm.changeAddress((uint8_t)addr, (uint8_t)get_int(root, "new_addr", 0));
    }
    else if (!strcmp(act, "update"))
    {
        e = pm.startUpdate((uint8_t)addr);
    }
    else if (!strcmp(act, "zones"))
    {
        cJSON *zones = cJSON_GetObjectItem(root, "zones");
        if (!cJSON_IsArray(zones) || cJSON_GetArraySize(zones) < NUM_ZONES || !s->zone_set)
            e = ModbusMaster::ERR_ARG;
        for (int ch = 0; e == 0 && ch < NUM_ZONES; ch++)
        {
            cJSON *z     = cJSON_GetArrayItem(zones, ch);
            int    probe = get_int(z, "probe", 0);
            int    thr   = get_int(z, "thr", 50);
            if (probe < 0 || probe > 247 || thr < 0 || thr > 100)
                e = ModbusMaster::ERR_ARG;
            else
                s->zone_set(ch, Zone{(uint8_t)probe, (uint8_t)thr});
        }
    }
    else
    {
        cJSON_Delete(root);
        return send_error(req, "unknown action");
    }

    cJSON_Delete(root);
    return send_result(req, e);
}

esp_err_t ProbeServer::fw_handler(httpd_req_t *req)
{
    ProbeServer  *s    = self_of(req);
    ProbeManager &pm   = s->m_probes;
    int           addr = query_int(req, "addr", 0);

    int e = pm.imageBegin(req->content_len);
    if (e)
    {
        // Drain the body so the browser gets the reply.
        char tmp[256];
        int  left = req->content_len;
        while (left > 0)
        {
            int n = httpd_req_recv(req, tmp, left < (int)sizeof(tmp) ? left : (int)sizeof(tmp));
            if (n <= 0)
                break;
            left -= n;
        }
        return send_result(req, e);
    }

    char buf[1024];
    int  left = req->content_len;
    while (left > 0)
    {
        int n = httpd_req_recv(req, buf, left < (int)sizeof(buf) ? left : (int)sizeof(buf));
        if (n == HTTPD_SOCK_ERR_TIMEOUT)
            continue;
        if (n <= 0)
            return send_error(req, "receive failed");
        if ((e = pm.imageWrite(buf, n)) != 0)
            return send_result(req, e);
        left -= n;
    }
    if ((e = pm.imageEnd()) != 0)
        return send_result(req, e);

    if (addr)
        e = pm.startUpdate((uint8_t)addr);
    return send_result(req, e);
}
