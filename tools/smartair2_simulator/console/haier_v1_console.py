#!/usr/bin/env python3
import json
import os
import subprocess
import time
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from urllib.parse import parse_qs

HOST = os.environ.get("HAIER_SIM_CONSOLE_HOST", "0.0.0.0")
PORT = int(os.environ.get("HAIER_SIM_CONSOLE_PORT", "18081"))
SERVICE = os.environ.get("HAIER_SIM_SERVICE", "haier-smartair2-simulator.service")
OLD_HON_SERVICE = os.environ.get("HAIER_SIM_OLD_HON_SERVICE", "haier-hon-simulator.service")
SERIAL = os.environ.get("HAIER_SIM_SERIAL", "/dev/ttyUSB0")
RUNTIME_DIR = os.environ.get("HAIER_SIM_RUNTIME_DIR", "/tmp/haier-smartair2-runtime")
CONTROL_FILE = f"{RUNTIME_DIR}/smartair2-control.env"
STATUS_FILE = f"{RUNTIME_DIR}/smartair2-status.json"
EVENT_FILE = f"{RUNTIME_DIR}/smartair2-events.jsonl"

DEFAULTS = {
    "power": "1",
    "mode": "cool",
    "fan_mode": "auto",
    "target_temperature": "16",
    "room_temperature": "20",
    "humidity": "45",
    "ambient_temperature": "28",
    "room_drift_temperature": "28",
    "dynamics": "1",
    "seconds_to_target": "60",
    "hold_seconds": "20",
    "drift_delta": "1",
    "drift_seconds": "40",
    "hysteresis": "0.7",
    "passive_seconds_per_degree": "70",
    "swing_mode": "off",
    "turbo_mode": "0",
    "quiet_mode": "0",
    "display_status": "0",
}

HTML = """<!doctype html>
<html lang="ru">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<meta http-equiv="Cache-Control" content="no-store">
<title>Haier v1 / SmartAir2 Control Console</title>
<style>
:root{color-scheme:dark;font-family:Inter,system-ui,Segoe UI,sans-serif;background:#101418;color:#eef2f6}
body{margin:0;padding:22px;background:#101418}
main{max-width:1180px;margin:auto}
h1{margin:0 0 6px;font-size:24px}
h2{font-size:17px;margin:0 0 12px}
section{background:#1a2028;border:1px solid #303946;border-radius:8px;padding:16px;margin:14px 0}
.grid{display:grid;grid-template-columns:repeat(4,minmax(140px,1fr));gap:10px}
.tile{background:#101820;border:1px solid #2b3542;border-radius:8px;padding:12px;min-height:58px}
.tile span{display:block;color:#9aa7b6;font-size:12px}
.tile b{display:block;font-size:20px;margin-top:4px}
.ok{color:#78e39c}.bad{color:#ff7b7b}.muted{color:#9aa7b6}
form{display:grid;grid-template-columns:repeat(4,minmax(160px,1fr));gap:12px;align-items:end}
label{display:flex;flex-direction:column;gap:6px;color:#b8c4d2;font-size:13px}
input,select,button{height:38px;border-radius:7px;border:1px solid #384555;background:#0f151c;color:#eef2f6;padding:0 10px;font:inherit}
input[type=checkbox]{height:18px;width:18px}
.check{flex-direction:row;align-items:center;height:38px}
button{background:#2e7be6;border-color:#3d8cff;font-weight:650;cursor:pointer}
button.secondary{background:#202a35;border-color:#384555}
pre{white-space:pre-wrap;word-break:break-word;background:#0b0f14;border-radius:8px;padding:12px;max-height:360px;overflow:auto;font-size:12px;line-height:1.45}
.row{display:flex;gap:10px;flex-wrap:wrap;align-items:center}
@media(max-width:860px){.grid,form{grid-template-columns:1fr 1fr}}
@media(max-width:560px){.grid,form{grid-template-columns:1fr}}
</style>
</head>
<body>
<main>
  <h1>Haier v1 / SmartAir2 Control Console</h1>
  <div class="muted">runtime console v1-control-20260912-2</div>
  <section>
    <h2>Статус стенда</h2>
    <div id="tiles" class="grid"></div>
  </section>
  <section>
    <h2>Управление симулятором</h2>
    <form id="control">
      <label class="check"><input name="power" type="checkbox"> Power</label>
      <label>Mode<select name="mode"><option>auto</option><option>cool</option><option>heat</option><option>fan</option><option>dry</option></select></label>
      <label>Fan<select name="fan_mode"><option>auto</option><option>low</option><option>medium</option><option>high</option></select></label>
      <label>Swing<select name="swing_mode"><option>off</option><option>vertical</option><option>horizontal</option><option>both</option></select></label>
      <label class="check"><input name="dynamics" type="checkbox"> Dynamics</label>
      <label>Current temp<input name="room_temperature" type="number" min="0" max="60" step="0.1"></label>
      <label>Target temp<input name="target_temperature" type="number" min="16" max="30" step="1"></label>
      <label>Room drift target<input name="room_drift_temperature" type="number" min="-30" max="60" step="0.1"></label>
      <label>Humidity<input name="humidity" type="number" min="0" max="100" step="1"></label>
      <label>Seconds to target<input name="seconds_to_target" type="number" min="1" max="600" step="1"></label>
      <label>Hold seconds<input name="hold_seconds" type="number" min="0" max="600" step="1"></label>
      <label>Drift delta<input name="drift_delta" type="number" min="0" max="10" step="0.1"></label>
      <label>Drift seconds<input name="drift_seconds" type="number" min="1" max="600" step="1"></label>
      <label>Hysteresis<input name="hysteresis" type="number" min="0.1" max="5" step="0.1"></label>
      <label>Passive sec/deg<input name="passive_seconds_per_degree" type="number" min="5" max="1200" step="1"></label>
      <label class="check"><input name="turbo_mode" type="checkbox"> Turbo</label>
      <label class="check"><input name="quiet_mode" type="checkbox"> Quiet</label>
      <label class="check"><input name="display_status" type="checkbox"> Display</label>
      <div class="row"><button type="submit">Apply</button><button class="secondary" type="button" id="baseline">Demo cool 16</button></div>
    </form>
  </section>
  <section><h2>События</h2><pre id="events">loading...</pre></section>
  <section><h2>Сервисный лог</h2><pre id="log">loading...</pre></section>
  <section><h2>Raw status</h2><pre id="raw">loading...</pre></section>
</main>
<script>
const form = document.getElementById('control');
let dirty = false;
form.addEventListener('input', () => dirty = true);

function value(name){ const el=form.elements[name]; return el.type === 'checkbox' ? (el.checked ? '1' : '0') : el.value; }
function fixedFor(name, v){
  if (v === undefined || v === null || v === '') return v;
  if (!['room_temperature','room_drift_temperature','ambient_temperature','target_temperature','humidity','seconds_to_target','hold_seconds','drift_delta','drift_seconds','hysteresis','passive_seconds_per_degree'].includes(name)) return v;
  const n = Number(v);
  if (!Number.isFinite(n)) return v;
  if (['target_temperature','humidity','seconds_to_target','hold_seconds','drift_seconds','passive_seconds_per_degree'].includes(name)) return String(Math.round(n));
  return (Math.round(n * 10) / 10).toFixed(1);
}
function fill(cfg){
  for (const [k,v] of Object.entries(cfg || {})) {
    const el = form.elements[k];
    if (!el || dirty) continue;
    if (el.type === 'checkbox') el.checked = !!v && v !== '0' && v !== 'false';
    else el.value = fixedFor(k, v);
  }
}
async function postControl(extra){
  const data = new URLSearchParams();
  for (const name of ['power','mode','fan_mode','swing_mode','dynamics','room_temperature','target_temperature','room_drift_temperature','humidity','seconds_to_target','hold_seconds','drift_delta','drift_seconds','hysteresis','passive_seconds_per_degree','turbo_mode','quiet_mode','display_status']) data.set(name, value(name));
  data.set('ambient_temperature', data.get('room_drift_temperature'));
  for (const [k,v] of Object.entries(extra || {})) data.set(k, v);
  const r = await fetch('/api/control', {method:'POST', body:data});
  if (!r.ok) alert(await r.text());
  dirty = false;
  await refresh();
}
form.addEventListener('submit', e => { e.preventDefault(); postControl(); });
document.getElementById('baseline').addEventListener('click', () => postControl({power:'1',mode:'cool',fan_mode:'auto',swing_mode:'off',dynamics:'1',room_temperature:'20',target_temperature:'16',room_drift_temperature:'28',ambient_temperature:'28',humidity:'45',seconds_to_target:'60',hold_seconds:'20',drift_delta:'1',drift_seconds:'40',hysteresis:'0.7',passive_seconds_per_degree:'70',turbo_mode:'0',quiet_mode:'0',display_status:'0'}));

async function refresh(){
  const s = await (await fetch('/api/status?ts=' + Date.now(), {cache:'no-store'})).json();
  document.getElementById('raw').textContent = JSON.stringify(s, null, 2);
  fill(Object.assign({}, s.control, s.sim || {}));
  const cls = s.service_active === 'active' ? 'ok' : 'bad';
  const phase = s.sim && s.sim.phase ? s.sim.phase : 'waiting';
  const room = s.sim && s.sim.room_temperature !== undefined ? Number(s.sim.room_temperature).toFixed(1) + '°' : '-';
  const target = s.sim && s.sim.target_temperature !== undefined ? s.sim.target_temperature + '°' : '-';
  const driftTarget = s.sim && s.sim.room_drift_temperature !== undefined ? s.sim.room_drift_temperature + '°' : (s.control.room_drift_temperature || s.control.ambient_temperature || '-');
  const mode = s.sim && s.sim.mode ? s.sim.mode : (s.control.mode || '-');
  const power = s.sim ? (s.sim.power ? 'on' : 'off') : (s.control.power === '1' ? 'on' : 'off');
  const fan = s.sim && s.sim.fan_mode ? s.sim.fan_mode : (s.control.fan_mode || '-');
  const swing = s.sim && s.sim.swing_mode ? s.sim.swing_mode : (s.control.swing_mode || '-');
  const compressor = s.sim ? (s.sim.compressor_active ? 'on' : 'off') : '-';
  const tiles = [
    ['v1 service', `<span class="${cls}">${s.service_active}</span>`],
    ['Serial', s.serial_exists ? '<span class="ok">ok</span>' : '<span class="bad">нет</span>'],
    ['Power', power],
    ['Mode', mode],
    ['Fan', fan],
    ['Swing', swing],
    ['Compressor', compressor],
    ['Current', room],
    ['Target', target],
    ['Drift target', driftTarget],
    ['Phase', phase],
    ['PID', s.pid || 'нет']
  ];
  document.getElementById('tiles').innerHTML = tiles.map(([a,b]) => `<div class="tile"><span>${a}</span><b>${b}</b></div>`).join('');
  document.getElementById('events').textContent = await (await fetch('/api/events?ts=' + Date.now(), {cache:'no-store'})).text();
  document.getElementById('log').textContent = await (await fetch('/api/log?ts=' + Date.now(), {cache:'no-store'})).text();
}
refresh();
setInterval(refresh, 2000);
</script>
</body>
</html>"""


def run(cmd):
    try:
        return subprocess.check_output(cmd, text=True, stderr=subprocess.STDOUT).strip()
    except subprocess.CalledProcessError as e:
        return e.output.strip()


def read_control():
    cfg = DEFAULTS.copy()
    try:
        with open(CONTROL_FILE, "r", encoding="utf-8") as f:
            for line in f:
                line = line.strip()
                if not line or line.startswith("#") or "=" not in line:
                    continue
                k, v = line.split("=", 1)
                if k in DEFAULTS:
                    cfg[k] = v
    except FileNotFoundError:
        pass
    return cfg


def write_control(cfg):
    os.makedirs(RUNTIME_DIR, exist_ok=True)
    merged = DEFAULTS.copy()
    merged.update({k: str(v) for k, v in cfg.items() if k in DEFAULTS})
    tmp = CONTROL_FILE + ".tmp"
    with open(tmp, "w", encoding="utf-8") as f:
        for k in DEFAULTS:
            f.write(f"{k}={merged[k]}\n")
    os.replace(tmp, CONTROL_FILE)
    return merged


def read_json_file(path):
    try:
        with open(path, "r", encoding="utf-8") as f:
            return json.load(f)
    except Exception:
        return {}


def tail(path, lines=80):
    try:
        with open(path, "r", encoding="utf-8", errors="replace") as f:
            data = f.readlines()[-lines:]
        return "".join(data)
    except FileNotFoundError:
        return ""


def status():
    return {
        "ts": int(time.time()),
        "protocol": "haier-v1-smartair2",
        "service": SERVICE,
        "service_active": run(["systemctl", "is-active", SERVICE]),
        "service_enabled": run(["systemctl", "is-enabled", SERVICE]),
        "old_hon_active": run(["systemctl", "is-active", OLD_HON_SERVICE]),
        "serial": SERIAL,
        "serial_exists": subprocess.call(["test", "-e", SERIAL]) == 0,
        "pid": run(["pgrep", "-f", "smartair2_simulator /dev/ttyUSB0"]),
        "control": read_control(),
        "sim": read_json_file(STATUS_FILE),
        "console": "v1 SmartAir2 control console",
    }


class Handler(BaseHTTPRequestHandler):
    def send_body(self, body, ctype="text/html; charset=utf-8", code=200):
        if isinstance(body, str):
            body = body.encode()
        self.send_response(code)
        self.send_header("Content-Type", ctype)
        self.send_header("Cache-Control", "no-store, max-age=0")
        self.send_header("Pragma", "no-cache")
        self.send_header("Content-Length", str(len(body)))
        self.end_headers()
        self.wfile.write(body)

    def do_GET(self):
        path = self.path.split("?", 1)[0]
        if path in ("/", "/index.html"):
            self.send_body(HTML)
        elif path == "/api/status":
            self.send_body(json.dumps(status(), ensure_ascii=False), "application/json; charset=utf-8")
        elif path == "/api/events":
            self.send_body(tail(EVENT_FILE, 120), "text/plain; charset=utf-8")
        elif path == "/api/log":
            self.send_body(run(["journalctl", "-u", SERVICE, "-n", "120", "--no-pager"]), "text/plain; charset=utf-8")
        else:
            self.send_error(404)

    def do_POST(self):
        if self.path.split("?", 1)[0] != "/api/control":
            self.send_error(404)
            return
        length = int(self.headers.get("Content-Length", "0"))
        fields = parse_qs(self.rfile.read(length).decode("utf-8"), keep_blank_values=True)
        cfg = {k: v[-1] for k, v in fields.items() if k in DEFAULTS}
        merged = write_control(cfg)
        self.send_body(json.dumps({"ok": True, "control": merged}, ensure_ascii=False), "application/json; charset=utf-8")

    def log_message(self, fmt, *args):
        return


if __name__ == "__main__":
    ThreadingHTTPServer((HOST, PORT), Handler).serve_forever()
