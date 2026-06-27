#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>
#include <WebSocketsServer.h>
#include <DNSServer.h>
#include <WiFiManager.h>

const char* ap_ssid  = "WindSensor";
const char* ap_pass  = "12345678";

#define HALL_PIN      4
#define MAGNETS       1
#define CAL_FACTOR    0.01878
#define MAX_SANE_MPH  150.0

ESP8266WebServer server(80);
WebSocketsServer webSocket(81);

volatile int pulseCount = 0;
int rpm = 0;
float mph = 0;
float maxMph = 0;
unsigned long lastCalc = 0;
unsigned long lastBroadcast = 0;
int ignoreCount = 4;

int rpmHistory[5] = {0};
int histIndex = 0;

void IRAM_ATTR hallISR() {
  unsigned long t = micros();
  static unsigned long lastPulse = 0;
  if (t - lastPulse > 5000) {
    pulseCount++;
    lastPulse = t;
  }
}

void webSocketEvent(uint8_t num, WStype_t type, uint8_t* payload, size_t length) {
  switch (type) {
    case WStype_DISCONNECTED:
      Serial.printf("[WS] Client #%u disconnected\n", num);
      break;
    case WStype_CONNECTED: {
      IPAddress ip = webSocket.remoteIP(num);
      Serial.printf("[WS] Client #%u connected from %s\n", num, ip.toString().c_str());
      String json = "{\"rpm\":"    + String(rpm)       +
                    ",\"mph\":"    + String(mph, 2)     +
                    ",\"maxMph\":" + String(maxMph, 2)  + "}";
      webSocket.sendTXT(num, json);
      break;
    }
    case WStype_TEXT:
      Serial.printf("[WS] Message from #%u: %s\n", num, payload);
      break;
    default:
      break;
  }
}

const char PAGE[] PROGMEM = R"rawhtml(
<!DOCTYPE html>
<html>
<head>
<meta charset="UTF-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>Wind Monitor</title>
<style>
  * { box-sizing: border-box; margin: 0; padding: 0; }
  body { background: #f5f5f0; font-family: Georgia, serif; color: #222; padding: 24px; max-width: 600px; margin: 0 auto; }
  h1 { font-size: 18px; font-weight: normal; color: #444; border-bottom: 1px solid #ddd; padding-bottom: 10px; margin-bottom: 24px; }
  .stats { display: grid; grid-template-columns: 1fr 1fr 1fr; gap: 16px; margin-bottom: 24px; }
  .card { background: #fff; border: 1px solid #ddd; border-radius: 4px; padding: 14px; text-align: center; }
  .card-label { font-size: 11px; color: #888; text-transform: uppercase; letter-spacing: .1em; margin-bottom: 6px; }
  .card-val { font-size: 32px; color: #222; line-height: 1; }
  .card-unit { font-size: 11px; color: #aaa; margin-top: 4px; }
  .reset-btn { margin-top: 8px; font-size: 10px; color: #aaa; background: none; border: 1px solid #ddd; border-radius: 3px; padding: 2px 8px; cursor: pointer; font-family: Georgia, serif; }
  .reset-btn:hover { border-color: #bbb; color: #888; }
  .chart-wrap { background: #fff; border: 1px solid #ddd; border-radius: 4px; padding: 16px; margin-bottom: 16px; }
  .chart-header { display: flex; justify-content: space-between; font-size: 11px; color: #888; margin-bottom: 12px; }
  canvas { width: 100%; display: block; }
  .status { font-size: 11px; color: #aaa; text-align: center; }
  .dot { display: inline-block; width: 6px; height: 6px; border-radius: 50%; background: #ccc; margin-right: 5px; vertical-align: middle; }
  .dot.live { background: #5a9; }
</style>
</head>
<body>
<h1>Wind Monitor</h1>
<div class="stats">
  <div class="card">
    <div class="card-label">Current</div>
    <div class="card-val" id="cur">0.00</div>
    <div class="card-unit">mph</div>
  </div>
  <div class="card">
    <div class="card-label">Max</div>
    <div class="card-val" id="max">0.00</div>
    <div class="card-unit">mph</div>
    <button class="reset-btn" onclick="resetMax()">Reset</button>
  </div>
  <div class="card">
    <div class="card-label">RPM</div>
    <div class="card-val" id="rpm">0</div>
    <div class="card-unit">rev/min</div>
  </div>
</div>
<div class="chart-wrap">
  <div class="chart-header">
    <span>Wind speed — last 10 min</span>
    <span id="elapsed">0:00</span>
  </div>
  <canvas id="chart" height="160"></canvas>
</div>
<div class="status"><span class="dot" id="dot"></span><span id="status">Connecting...</span></div>

<script>
const MAX_PTS = 1200;
let data = [], startTime = Date.now();
let canvas = document.getElementById('chart');
let ctx = canvas.getContext('2d');

const wsHost = location.hostname;
let ws;

function connectWS() {
  ws = new WebSocket('ws://' + wsHost + ':81/');

  ws.onopen = () => {
    document.getElementById('dot').className = 'dot live';
    document.getElementById('status').textContent = 'Live';
  };

  ws.onmessage = (event) => {
    const d = JSON.parse(event.data);
    document.getElementById('cur').textContent = d.mph.toFixed(2);
    document.getElementById('rpm').textContent = d.rpm;
    document.getElementById('max').textContent = d.maxMph.toFixed(2);
    data.push({ mph: d.mph });
    if (data.length > MAX_PTS) data.shift();
    draw();
    const e = Math.floor((Date.now() - startTime) / 1000);
    document.getElementById('elapsed').textContent =
      Math.floor(e / 60) + ':' + (e % 60 < 10 ? '0' : '') + e % 60;
  };

  ws.onclose = () => {
    document.getElementById('dot').className = 'dot';
    document.getElementById('status').textContent = 'Reconnecting...';
    setTimeout(connectWS, 2000);
  };

  ws.onerror = () => {
    ws.close();
  };
}

function resetMax() {
  fetch('/resetmax').catch(() => {});
}

function resize() {
  canvas.width  = canvas.offsetWidth * devicePixelRatio;
  canvas.height = 160 * devicePixelRatio;
  ctx.scale(devicePixelRatio, devicePixelRatio);
  draw();
}
window.addEventListener('resize', resize);

function draw() {
  const W = canvas.offsetWidth, H = 160;
  ctx.clearRect(0, 0, W, H);
  const pad = {top:8, right:8, bottom:24, left:36};
  const gW = W - pad.left - pad.right;
  const gH = H - pad.top  - pad.bottom;
  const peak = Math.max(...data.map(d=>d.mph), 5);
  const yMax = peak * 1.2;

  ctx.strokeStyle = '#eee';
  ctx.lineWidth = 1;
  ctx.fillStyle = '#aaa';
  ctx.font = '10px Georgia, serif';
  ctx.textAlign = 'right';
  for (let i = 0; i <= 4; i++) {
    const y = pad.top + gH - (i/4)*gH;
    ctx.beginPath(); ctx.moveTo(pad.left, y); ctx.lineTo(pad.left+gW, y); ctx.stroke();
    ctx.fillText((yMax*i/4).toFixed(1), pad.left-4, y+3);
  }

  ctx.textAlign = 'center';
  ['-10:00','-8:00','-6:00','-4:00','-2:00','now'].forEach((l,i,a) => {
    ctx.fillText(l, pad.left+(i/(a.length-1))*gW, H-6);
  });

  if (data.length < 2) return;

  ctx.beginPath();
  data.forEach((d,i) => {
    const x = pad.left+(i/(MAX_PTS-1))*gW;
    const y = pad.top+gH-(d.mph/yMax)*gH;
    i===0 ? ctx.moveTo(x,y) : ctx.lineTo(x,y);
  });
  ctx.lineTo(pad.left+((data.length-1)/(MAX_PTS-1))*gW, pad.top+gH);
  ctx.lineTo(pad.left, pad.top+gH);
  ctx.closePath();
  ctx.fillStyle = 'rgba(80,140,100,0.15)';
  ctx.fill();

  ctx.beginPath();
  data.forEach((d,i) => {
    const x = pad.left+(i/(MAX_PTS-1))*gW;
    const y = pad.top+gH-(d.mph/yMax)*gH;
    i===0 ? ctx.moveTo(x,y) : ctx.lineTo(x,y);
  });
  ctx.strokeStyle = '#5a9';
  ctx.lineWidth = 1.5;
  ctx.lineJoin = 'round';
  ctx.stroke();
}

resize();
connectWS();
</script>
</body>
</html>
)rawhtml";

void setup() {
  Serial.begin(115200);
  pinMode(HALL_PIN, INPUT_PULLUP);
  attachInterrupt(digitalPinToInterrupt(HALL_PIN), hallISR, FALLING);

  WiFiManager wifiManager;
  wifiManager.setConfigPortalTimeout(60); // wait 60s for config, then continue anyway
  wifiManager.autoConnect("WindSensor-Setup", "12345678");

  Serial.println("WiFi connected, IP: " + WiFi.localIP().toString());

  // Always also run as AP so you can access it directly
  WiFi.softAP(ap_ssid, ap_pass);
  Serial.println("AP IP: 192.168.4.1");

  server.on("/", []() {
    server.send_P(200, "text/html", PAGE);
  });

  server.on("/rpm", []() {
    server.sendHeader("Access-Control-Allow-Origin", "*");
    String json = "{\"rpm\":"    + String(rpm)       +
                  ",\"mph\":"    + String(mph, 2)     +
                  ",\"pulses\":" + String(pulseCount) + "}";
    server.send(200, "application/json", json);
  });

  server.on("/resetmax", []() {
    server.sendHeader("Access-Control-Allow-Origin", "*");
    maxMph = 0;
    server.send(200, "application/json", "{\"ok\":true}");
  });

  server.begin();

  webSocket.begin();
  webSocket.onEvent(webSocketEvent);

  Serial.println("Server + WebSocket started");
}

void loop() {
  server.handleClient();
  webSocket.loop();

  unsigned long now = millis();

  if (now - lastCalc >= 500) {
    int count = pulseCount;
    pulseCount = 0;

    if (ignoreCount > 0) {
      ignoreCount--;
      lastCalc = now;
      return;
    }

    int rawRPM = (count * 60 * 2) / MAGNETS;
    if (count == 0) rawRPM = 0;

    rpmHistory[histIndex % 5] = rawRPM;
    histIndex++;
    int smoothRPM = 0;
    for (int i = 0; i < 5; i++) smoothRPM += rpmHistory[i];
    smoothRPM /= 5;
    rpm = smoothRPM;

    float rawMph = rpm * CAL_FACTOR;
    if (rawMph > MAX_SANE_MPH) {
      mph = 0;
      rpm = 0;
    } else {
      mph = rawMph;
      if (mph > maxMph) maxMph = mph;
    }

    lastCalc = now;

    Serial.print("RPM: ");
    Serial.print(rpm);
    Serial.print("  MPH: ");
    Serial.println(mph, 2);
  }

  if (now - lastBroadcast >= 500) {
    if (webSocket.connectedClients() > 0) {
      String json = "{\"rpm\":"    + String(rpm)       +
                    ",\"mph\":"    + String(mph, 2)     +
                    ",\"maxMph\":" + String(maxMph, 2)  + "}";
      webSocket.broadcastTXT(json);
    }
    lastBroadcast = now;
  }
}
