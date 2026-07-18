/*
 * Oil Level Sensor Test - ESP32 C3 SuperMini
 * 
 * Tests a 3-reed-switch oil level sensor:
 *   Pin 1 (Uv) → GND (sensor common)
 *   Pin 2 (Min) → GPIO2 (Schließer / NO — pulls LOW when magnet present)
 *   Pin 3 (Min/Max) → GPIO3 (Öffner / NC — pulls LOW when magnet present)
 *   Pin 4 (Max) → GPIO4 (Öffner / NC — pulls LOW when magnet present)
 * 
 * ESP32 internal pull-ups enabled, no external components needed.
 * Pure Access Point — no external network.
 * Web UI shows live switch states with auto-refresh.
 * Onboard LED lights up when any switch is activated.
 */

#include <WiFi.h>
#include <WebServer.h>
#include <DNSServer.h>

// --- Configuration ---
const char* AP_SSID = "OilSensorTest";
const char* AP_PASS = NULL;  // open network, no password

// GPIO pins (ESP32 C3 SuperMini)
const int PIN_MIN    = 2;   // Reed 1: Schließer (NO)
const int PIN_MINMAX = 3;   // Reed 2: Öffner (NC)
const int PIN_MAX    = 4;   // Reed 3: Öffner (NC)
const int PIN_LED    = 8;   // Onboard LED (active LOW)

WebServer server(80);
DNSServer dnsServer;
const byte DNS_PORT = 53;

// --- HTML Page ---
const char PAGE_HTML[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html lang="de">
<head>
<meta charset="UTF-8">
<meta name="viewport" content="width=device-width, initial-scale=1.0">
<title>Ölstand-Sensor Test</title>
<style>
  * { box-sizing: border-box; margin: 0; padding: 0; }
  body {
    font-family: -apple-system, BlinkMacSystemFont, 'Segoe UI', sans-serif;
    background: #1a1a2e;
    color: #eee;
    min-height: 100vh;
    display: flex;
    flex-direction: column;
    align-items: center;
    padding: 20px;
  }
  h1 {
    font-size: 1.5em;
    margin-bottom: 8px;
    color: #e94560;
  }
  .subtitle {
    color: #888;
    font-size: 0.85em;
    margin-bottom: 24px;
  }
  .status-bar {
    background: #16213e;
    border-radius: 10px;
    padding: 10px 20px;
    margin-bottom: 20px;
    font-size: 0.9em;
    display: flex;
    gap: 20px;
    align-items: center;
  }
  .status-bar .dot {
    width: 10px; height: 10px;
    border-radius: 50%;
    background: #4ecca3;
    display: inline-block;
    animation: pulse 1.5s infinite;
  }
  @keyframes pulse {
    0%, 100% { opacity: 1; }
    50% { opacity: 0.3; }
  }
  .cards {
    display: flex;
    flex-direction: column;
    gap: 16px;
    width: 100%;
    max-width: 400px;
  }
  .card {
    background: #16213e;
    border-radius: 14px;
    padding: 20px;
    display: flex;
    align-items: center;
    gap: 16px;
    transition: background 0.3s;
    border: 2px solid transparent;
  }
  .card.active {
    border-color: #4ecca3;
    background: #1a2744;
  }
  .card.active-min {
    border-color: #e94560;
    background: #2a1a2e;
  }
  .lamp {
    width: 50px; height: 50px;
    border-radius: 50%;
    flex-shrink: 0;
    transition: all 0.3s;
    box-shadow: inset 0 2px 6px rgba(0,0,0,0.4);
  }
  .lamp.off {
    background: #333;
  }
  .lamp.on-green {
    background: #4ecca3;
    box-shadow: 0 0 20px #4ecca3, inset 0 2px 6px rgba(0,0,0,0.2);
  }
  .lamp.on-red {
    background: #e94560;
    box-shadow: 0 0 20px #e94560, inset 0 2px 6px rgba(0,0,0,0.2);
  }
  .info {
    flex: 1;
  }
  .info .name {
    font-size: 1.1em;
    font-weight: 600;
    margin-bottom: 2px;
  }
  .info .type {
    font-size: 0.8em;
    color: #888;
  }
  .info .state {
    font-size: 0.9em;
    margin-top: 4px;
    font-weight: 500;
  }
  .info .state.on { color: #4ecca3; }
  .info .state.off { color: #e94560; }
  .gpio {
    font-size: 0.7em;
    color: #555;
    font-family: monospace;
  }
</style>
</head>
<body>
<h1>🛢️ Ölstand-Sensor Test</h1>
<p class="subtitle">Reed-Kontakte Live-Anzeige</p>

<div class="status-bar">
  <span class="dot" id="liveDot"></span>
  <span>Live — alle 200ms</span>
  <span id="ipInfo" style="color:#555"></span>
</div>

<div class="cards">
  <div class="card" id="cardMin">
    <div class="lamp off" id="lampMin"></div>
    <div class="info">
      <div class="name">Min.</div>
      <div class="type">Schließer (NO)</div>
      <div class="state off" id="stateMin">—</div>
      <div class="gpio">GPIO2</div>
    </div>
  </div>

  <div class="card" id="cardMinMax">
    <div class="lamp off" id="lampMinMax"></div>
    <div class="info">
      <div class="name">Nachfüllen</div>
      <div class="type">Öffner (NC) — Min/Max</div>
      <div class="state off" id="stateMinMax">—</div>
      <div class="gpio">GPIO3</div>
    </div>
  </div>

  <div class="card" id="cardMax">
    <div class="lamp off" id="lampMax"></div>
    <div class="info">
      <div class="name">Max.</div>
      <div class="type">Öffner (NC)</div>
      <div class="state off" id="stateMax">—</div>
      <div class="gpio">GPIO4</div>
    </div>
  </div>
</div>

<script>
function update() {
  fetch('/status')
    .then(r => r.json())
    .then(d => {
      setLamp('lampMin', 'cardMin', 'stateMin', d.min, 'Betätigt', 'Nicht betätigt');
      setLamp('lampMinMax', 'cardMinMax', 'stateMinMax', d.minmax, 'Betätigt', 'Nicht betätigt');
      setLamp('lampMax', 'cardMax', 'stateMax', d.max, 'Betätigt', 'Nicht betätigt');
    })
    .catch(() => {
      document.getElementById('liveDot').style.background = '#e94560';
    });
}

function setLamp(lampId, cardId, stateId, active, onText, offText) {
  const lamp = document.getElementById(lampId);
  const card = document.getElementById(cardId);
  const state = document.getElementById(stateId);

  if (active) {
    lamp.className = 'lamp on-green';
    card.className = cardId === 'cardMin' ? 'card active-min' : 'card active';
    state.className = 'state on';
    state.textContent = onText;
  } else {
    lamp.className = 'lamp off';
    card.className = 'card';
    state.className = 'state off';
    state.textContent = offText;
  }
}

setInterval(update, 200);
update();
</script>
</body>
</html>
)rawliteral";

void handleCaptivePortal() {
  // Redirect ALL requests (including OS detection URLs) to the setup page.
  // The 302 redirect tells the OS "you're behind a captive portal" → triggers the popup.
  server.sendHeader("Location", String("http://") + WiFi.softAPIP().toString() + "/", true);
  server.send(302, "text/plain", "");
}

// --- Handlers ---
void handleRoot() {
  server.send_P(200, "text/html", PAGE_HTML);
}

void handleStatus() {
  bool minActive    = (digitalRead(PIN_MIN) == LOW);     // Schließer: closed = active
  bool minmaxActive = (digitalRead(PIN_MINMAX) == HIGH);  // Öffner: open = active
  bool maxActive    = (digitalRead(PIN_MAX) == HIGH);     // Öffner: open = active

  // Onboard LED: ON if any switch is activated
  bool anyActive = minActive || minmaxActive || maxActive;
  digitalWrite(PIN_LED, anyActive ? LOW : HIGH);  // active LOW

  String json = "{";
  json += "\"min\":" + String(minActive ? "true" : "false") + ",";
  json += "\"minmax\":" + String(minmaxActive ? "true" : "false") + ",";
  json += "\"max\":" + String(maxActive ? "true" : "false");
  json += "}";

  server.send(200, "application/json", json);
}

void setup() {
  Serial.begin(115200);
  Serial.println("\n=== Oil Level Sensor Test ===");

  // Configure GPIOs
  pinMode(PIN_MIN, INPUT_PULLUP);
  pinMode(PIN_MINMAX, INPUT_PULLUP);
  pinMode(PIN_MAX, INPUT_PULLUP);
  pinMode(PIN_LED, OUTPUT);
  digitalWrite(PIN_LED, HIGH);  // LED off (active LOW)

  // Start Access Point
  WiFi.mode(WIFI_AP);
  WiFi.softAP(AP_SSID, AP_PASS);
  delay(500);

  IPAddress ip = WiFi.softAPIP();
  Serial.print("AP started: ");
  Serial.println(AP_SSID);
  Serial.print("IP: ");
  Serial.println(ip);

  // Setup web server
  server.on("/", handleRoot);
  server.on("/status", handleStatus);
  // Captive portal detection — answer all known OS probe URLs
  server.on("/generate_204", HTTP_GET, handleCaptivePortal);
  server.on("/gen_204", HTTP_GET, handleCaptivePortal);
  server.on("/hotspot-detect.html", HTTP_GET, handleCaptivePortal);
  server.on("/success.txt", HTTP_GET, handleCaptivePortal);
  server.on("/connecttest.txt", HTTP_GET, handleCaptivePortal);
  server.on("/ncsi.txt", HTTP_GET, handleCaptivePortal);
  server.on("/redirect", HTTP_GET, handleCaptivePortal);
  server.on("/kindle-wifi/wifistub.html", HTTP_GET, handleCaptivePortal);
  server.onNotFound(handleCaptivePortal);  // catch-all → redirect
  server.begin();
  Serial.println("Web server started on port 80");

  // Start DNS server — redirects ALL domains to the AP IP
  dnsServer.start(DNS_PORT, "", WiFi.softAPIP());
  Serial.println("DNS server started — captive portal active");
}

void loop() {
  server.handleClient();
  dnsServer.processNextRequest();
}
