/**
 * ESP32 Insulin Pump Simulation
 * Features: Bolus, Basal Rate, State Persistence (NVS)
 */
#include <Arduino.h>
#include <WiFi.h>
#include <AsyncTCP.h>
#include <ESPAsyncWebServer.h>
#include <ESP32Servo.h>
#include <Preferences.h> // For saving state across reboots

// ==========================================
// CONFIGURATION
// ==========================================
const char* ssid = "<NETWORK_SSID>";
const char* password = "<WIFI-PASSWORD>";

#define SERVO_PIN 18
#define BUTTON_PIN 4

const float DOSE_INCREMENT = 0.05;   // Units per motor tick
const int TICK_INTERVAL_MS = 1000;   // 1 second between bolus ticks

// Global State Variables (Stored in NVS)
float totalCapacity = 315.0; 
float unitsDelivered = 0.0;
float unitsRemaining = 315.0;
float basalRateUph = 0.0;            // Basal Rate in Units per Hour
bool isReservoirEmpty = false;

// Async Pumping Variables
float pendingUnits = 0.0;            // Bolus queue
unsigned long lastBolusTick = 0;     
unsigned long lastBasalTick = 0;
bool isPumping = false;              

// NVS Saving Variables
Preferences preferences;
bool stateDirty = false;
unsigned long lastSaveTime = 0;
const unsigned long SAVE_INTERVAL_MS = 30000; // Save to flash every 30s if changed

// Hardware Objects
Servo pumpServo;
AsyncWebServer server(80);
AsyncEventSource events("/events");

// ==========================================
// WEB INTERFACE
// ==========================================
const char index_html[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html lang="en">
<head>
  <meta charset="UTF-8">
  <meta name="viewport" content="width=device-width, initial-scale=1.0">
  <title>ESP32 Pump Dashboard</title>
  <style>
    body { font-family: 'Segoe UI', Arial, sans-serif; background: #f4f7f6; text-align: center; margin: 0; padding: 20px; }
    .container { max-width: 450px; margin: 0 auto; background: white; padding: 25px; border-radius: 15px; box-shadow: 0 4px 15px rgba(0,0,0,0.1); }
    h2 { color: #333; margin-top: 0; }
    .card { background: #eef2f3; border-radius: 10px; padding: 15px; margin-bottom: 15px; }
    .label { font-size: 0.85rem; color: #666; text-transform: uppercase; letter-spacing: 1px; font-weight: bold; }
    .value { font-size: 2rem; font-weight: bold; color: #2c3e50; }
    .unit { font-size: 1rem; color: #7f8c8d; font-weight: normal; }
    
    .grid { display: grid; grid-template-columns: 1fr 1fr; gap: 10px; }
    input[type=number] { width: 100%; padding: 10px; font-size: 1rem; border: 2px solid #ddd; border-radius: 8px; box-sizing: border-box; text-align: center; }
    
    button { background: #3498db; color: white; border: none; padding: 12px; font-size: 1rem; border-radius: 8px; cursor: pointer; width: 100%; font-weight: bold; transition: 0.2s; }
    button:hover { background: #2980b9; }
    button:active { transform: scale(0.98); }
    button:disabled { background: #bdc3c7; cursor: not-allowed; }
    
    .btn-danger { background: #e74c3c; margin-top: 15px; }
    .btn-danger:hover { background: #c0392b; }

    .alert { background: #e74c3c; color: white; padding: 15px; border-radius: 10px; margin-top: 15px; display: none; font-weight: bold; }
    #progress-bar { width: 100%; height: 12px; background: #ddd; border-radius: 6px; margin-top: 10px; overflow: hidden; }
    #progress-fill { height: 100%; background: #2ecc71; width: 100%; transition: width 0.5s; }
    
    hr { border: 0; height: 1px; background: #ddd; margin: 25px 0; }
  </style>
</head>
<body>
  <div class="container">
    <h2>Pump Dashboard</h2>
    
    <div class="card">
      <div class="label">Reservoir Status</div>
      <div class="value"><span id="u-rem">0.00</span> <span class="unit">/ <span id="u-cap">0.00</span> U</span></div>
      <div id="progress-bar"><div id="progress-fill"></div></div>
      <div style="margin-top:5px; font-size:0.85rem; color:#888;"><span id="pct">100</span>% Remaining</div>
    </div>

    <div class="grid">
      <div class="card">
        <div class="label">Total Delivered</div>
        <div class="value" style="font-size: 1.5rem;"><span id="u-del">0.00</span></div>
      </div>
      <div class="card">
        <div class="label">Current Basal</div>
        <div class="value" style="font-size: 1.5rem;"><span id="u-basal">0.00</span><span class="unit">U/hr</span></div>
      </div>
    </div>

    <hr>

    <div class="label" style="margin-bottom: 10px;">Bolus Delivery</div>
    <div class="grid">
      <input type="number" id="bolus-input" value="5.00" step="0.05" min="0.05">
      <button id="bolus-btn" onclick="sendRequest('/deliver?amount=', 'bolus-input')">Deliver Bolus</button>
    </div>

    <div class="label" style="margin-top: 20px; margin-bottom: 10px;">Basal Configuration</div>
    <div class="grid">
      <input type="number" id="basal-input" value="1.00" step="0.05" min="0.00">
      <button onclick="sendRequest('/setBasal?rate=', 'basal-input')">Set Basal</button>
    </div>

    <hr>
    
    <div class="label" style="margin-bottom: 10px;">System Reset</div>
    <div class="grid">
      <input type="number" id="capacity-input" value="315" step="1" min="10">
      <button class="btn-danger" onclick="confirmReset()">Fill & Reset</button>
    </div>

    <div id="alert-box" class="alert">RESERVOIR EMPTY</div>
  </div>

  <script>
    if (!!window.EventSource) {
      var source = new EventSource('/events');
      
      source.addEventListener('update', function(e) {
        var data = JSON.parse(e.data);
        
        document.getElementById("u-del").innerHTML = data.delivered.toFixed(2);
        document.getElementById("u-rem").innerHTML = data.remaining.toFixed(2);
        document.getElementById("u-cap").innerHTML = data.capacity.toFixed(0);
        document.getElementById("u-basal").innerHTML = data.basal.toFixed(2);
        
        var pct = (data.remaining / data.capacity) * 100;
        document.getElementById("pct").innerHTML = pct.toFixed(1);
        document.getElementById("progress-fill").style.width = Math.max(0, Math.min(100, pct)) + "%";

        var btn = document.getElementById("bolus-btn");
        if (data.empty) {
          btn.disabled = true;
          btn.innerText = "Empty";
          document.getElementById("alert-box").style.display = "block";
        } else if (data.pumping) {
          btn.disabled = true;
          btn.innerText = "Bolusing... (" + data.pending.toFixed(2) + ")";
          document.getElementById("alert-box").style.display = "none";
        } else {
          btn.disabled = false;
          btn.innerText = "Deliver Bolus";
          document.getElementById("alert-box").style.display = "none";
        }
      }, false);
    }

    function sendRequest(route, inputId) {
      var val = document.getElementById(inputId).value;
      fetch(route + val)
        .then(res => { if (res.status !== 200) alert("Request failed or pump busy."); })
        .catch(err => console.error(err));
    }

    function confirmReset() {
      if(confirm("This will reset delivered units to 0 and set the reservoir to the input capacity. Continue?")) {
        sendRequest('/resetPump?capacity=', 'capacity-input');
      }
    }
  </script>
</body>
</html>
)rawliteral";

// ==========================================
// CORE LOGIC
// ==========================================

void saveStateToNVS() {
  preferences.putFloat("deliv", unitsDelivered);
  preferences.putFloat("rem", unitsRemaining);
  preferences.putFloat("cap", totalCapacity);
  preferences.putFloat("basal", basalRateUph);
  preferences.putBool("empty", isReservoirEmpty);
  Serial.println("[NVS] System state saved to flash memory.");
  stateDirty = false;
}

void loadStateFromNVS() {
  unitsDelivered = preferences.getFloat("deliv", 0.0);
  totalCapacity = preferences.getFloat("cap", 315.0);
  unitsRemaining = preferences.getFloat("rem", totalCapacity);
  basalRateUph = preferences.getFloat("basal", 0.0);
  isReservoirEmpty = preferences.getBool("empty", false);
  Serial.printf("[NVS] State Loaded -> Rem: %.2f, Basal: %.2f U/hr\n", unitsRemaining, basalRateUph);
}

void updateClients() {
  String json = "{";
  json += "\"delivered\":" + String(unitsDelivered, 2) + ",";
  json += "\"remaining\":" + String(unitsRemaining, 2) + ",";
  json += "\"capacity\":" + String(totalCapacity, 2) + ",";
  json += "\"basal\":" + String(basalRateUph, 2) + ",";
  json += "\"empty\":" + String(isReservoirEmpty ? "true" : "false") + ",";
  json += "\"pumping\":" + String(isPumping ? "true" : "false") + ",";
  json += "\"pending\":" + String(pendingUnits, 2);
  json += "}";
  events.send(json.c_str(), "update", millis());
}

void triggerSingleTick(String type) {
  if (isReservoirEmpty || unitsRemaining <= 0) {
    isReservoirEmpty = true;
    isPumping = false;
    stateDirty = true;
    updateClients(); 
    return;
  }

  unitsDelivered += DOSE_INCREMENT;
  unitsRemaining -= DOSE_INCREMENT;
  stateDirty = true; // Flag that we need to save to NVS eventually

  // Physical Movement
  pumpServo.writeMicroseconds(2000); 
  delay(150);                        
  pumpServo.writeMicroseconds(1500); 

  Serial.printf("[%s] Tick. Rem: %.2f U\n", type.c_str(), unitsRemaining);
  updateClients();
}

// Calculate milliseconds between basal ticks based on hourly rate
unsigned long getBasalIntervalMs() {
  if (basalRateUph <= 0.0) return 0xFFFFFFFF; // Effectively infinite / disabled
  // ticks per hour = basalRateUph / DOSE_INCREMENT
  // ms per tick = 3600000 / ticks_per_hour
  return (unsigned long)(3600000.0 / (basalRateUph / DOSE_INCREMENT));
}

// ==========================================
// SETUP
// ==========================================
void setup() {
  Serial.begin(115200);

  // 1. Load NVS Preferences
  preferences.begin("pump-state", false);
  loadStateFromNVS();

  // 2. Hardware Setup
  pumpServo.setPeriodHertz(50); 
  pumpServo.attach(SERVO_PIN); 
  pumpServo.writeMicroseconds(1500); 
  pinMode(BUTTON_PIN, INPUT_PULLUP);

  // 3. WiFi Setup
  Serial.print("Connecting to WiFi");
  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) { delay(500); Serial.print("."); }
  Serial.println("\nConnected! IP: " + WiFi.localIP().toString());

  // 4. Web Routes
  server.on("/", HTTP_GET, [](AsyncWebServerRequest *request){
    request->send_P(200, "text/html", index_html);
  });

  server.on("/deliver", HTTP_GET, [](AsyncWebServerRequest *request){
    if (request->hasParam("amount") && !isPumping && !isReservoirEmpty) {
      pendingUnits = request->getParam("amount")->value().toFloat();
      isPumping = true;         
      lastBolusTick = millis(); 
      request->send(200, "text/plain", "OK");
      updateClients();
    } else { request->send(400, "text/plain", "Busy/Empty"); }
  });

  server.on("/setBasal", HTTP_GET, [](AsyncWebServerRequest *request){
    if (request->hasParam("rate")) {
      basalRateUph = request->getParam("rate")->value().toFloat();
      lastBasalTick = millis(); // Reset timer so we don't instantly dose
      stateDirty = true;        // Flag to save
      Serial.printf("[WEB] Basal set to %.2f U/hr\n", basalRateUph);
      request->send(200, "text/plain", "OK");
      updateClients();
      saveStateToNVS();         // Force save on configuration changes
    }
  });

  server.on("/resetPump", HTTP_GET, [](AsyncWebServerRequest *request){
    if (request->hasParam("capacity")) {
      totalCapacity = request->getParam("capacity")->value().toFloat();
      unitsRemaining = totalCapacity;
      unitsDelivered = 0.0;
      isReservoirEmpty = false;
      isPumping = false; // Cancel active bolus
      pendingUnits = 0.0;
      
      Serial.printf("[WEB] Pump Reset. Capacity: %.2f U\n", totalCapacity);
      request->send(200, "text/plain", "OK");
      updateClients();
      saveStateToNVS(); // Force save
    }
  });

  events.onConnect([](AsyncEventSourceClient *client){
    client->send("hello!", NULL, millis(), 1000);
  });
  server.addHandler(&events);
  server.begin();
}

// ==========================================
// MAIN LOOP
// ==========================================
void loop() {
  // 1. MANUAL BUTTON PRIME
  static int lastBtnState = HIGH;
  int btnState = digitalRead(BUTTON_PIN);
  if (lastBtnState == HIGH && btnState == LOW && !isPumping) {
    triggerSingleTick("PRIME");
    delay(200); 
  }
  lastBtnState = btnState;

  // 2. BOLUS STATE MACHINE
  if (isPumping) {
    if (millis() - lastBolusTick >= TICK_INTERVAL_MS) {
      if (pendingUnits > 0.01 && !isReservoirEmpty) {
        triggerSingleTick("BOLUS");
        pendingUnits -= DOSE_INCREMENT;
        lastBolusTick = millis(); 
      } 
      if (pendingUnits <= 0.01) {
        pendingUnits = 0.0;
        isPumping = false;
        Serial.println("[SYSTEM] Bolus complete.");
        updateClients();
      }
    }
  }

  // 3. BASAL STATE MACHINE (Only runs if rate > 0 and reservoir is not empty)
  if (basalRateUph > 0.01 && !isReservoirEmpty) {
    unsigned long basalInterval = getBasalIntervalMs();
    if (millis() - lastBasalTick >= basalInterval) {
      // Small safety wrapper: don't execute a basal tick at the exact millisecond a bolus is ticking
      if (!isPumping || (millis() - lastBolusTick > 200)) { 
        triggerSingleTick("BASAL");
        lastBasalTick = millis();
      }
    }
  }

  // 4. PERIODIC NVS SAVE
  if (stateDirty && (millis() - lastSaveTime >= SAVE_INTERVAL_MS)) {
    saveStateToNVS();
    lastSaveTime = millis();
  }

  // 5. KEEP-ALIVE UI UPDATES
  static unsigned long lastUpdate = 0;
  if (millis() - lastUpdate > 3000) { 
    updateClients();
    lastUpdate = millis();
  }
  
  delay(10); 
}