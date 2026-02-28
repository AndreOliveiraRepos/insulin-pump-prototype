/**
 * ESP32 Insulin Pump - AndroidAPS API Compatible
 * Mechanics: 40:1 Worm Drive, 15T Pinion, 40mm stroke = 315 Units
 * Features: REST API, JSON, Temp Basal, Suspend, Audible Beeps, Auto-Rewind
 */
#include <Arduino.h>
#include <WiFi.h>
#include <AsyncTCP.h>
#include <ESPAsyncWebServer.h>
#include <AsyncJson.h>
#include <ArduinoJson.h>
#include <ESP32Servo.h>
#include <Preferences.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SH110X.h>
#include <time.h> 

// ==========================================
// CONFIGURATION
// ==========================================
const char* ssid = "<NETWORK_SSID>";
const char* password = "<WIFI-PASSWORD>";

#define SERVO_PIN 18
#define BUTTON_PIN 4
#define BUZZER_PIN 25 // Piezo buzzer for feedback

// SH1106 OLED Configuration
#define i2c_Address 0x3c 
#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
#define OLED_RESET -1
Adafruit_SH1106G display = Adafruit_SH1106G(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);

// Pump Physics & Mechanics (40:1 Worm Drive)
const float TOTAL_UNITS = 315.0;     
const float DOSE_INCREMENT = 0.5;    
const int TICK_DURATION_MS = 55;     // Motor run time for 0.5U (19.3 degrees)
const int TICK_INTERVAL_MS = 1000;   // 1 second gap between bolus ticks

// Continuous Servo Commands
const int SERVO_STOP = 1500;
const int SERVO_FORWARD = 2000;
const int SERVO_REVERSE = 1000;      

// Standard Variables
float totalCapacity = TOTAL_UNITS;
float unitsDelivered = 0.0;
float unitsRemaining = TOTAL_UNITS;
float basalRateUph = 0.0;            
float lastBolusAmount = 0.0; 
bool isReservoirEmpty = false;

// API & State Variables
bool isPumping = false;              
float pendingUnits = 0.0;            
unsigned long lastBolusTick = 0;     
bool isSuspended = false;

// Temp Basal Variables
bool isTempBasalActive = false;
float tempBasalRate = 0.0;
unsigned long tempBasalEndMillis = 0;
unsigned long lastBasalTick = 0;

// Rewind Variables (Mechanical Reset)
bool isRewinding = false;
unsigned long rewindStartTime = 0;
unsigned long rewindDuration = 0;

// NVS Saving Variables
Preferences preferences;
bool stateDirty = false;
unsigned long lastSaveTime = 0;
const unsigned long SAVE_INTERVAL_MS = 30000; 

// Hardware Objects
Servo pumpServo;
AsyncWebServer server(80);
AsyncEventSource events("/events");

// ==========================================
// WEB INTERFACE (HTML)
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
      <div class="value"><span id="u-rem">0.0</span> <span class="unit">/ <span id="u-cap">0</span> U</span></div>
      <div id="progress-bar"><div id="progress-fill"></div></div>
      <div style="margin-top:5px; font-size:0.85rem; color:#888;"><span id="pct">100</span>% Remaining</div>
    </div>
    <div class="grid">
      <div class="card"><div class="label">Total Delivered</div><div class="value" style="font-size: 1.5rem;"><span id="u-del">0.0</span></div></div>
      <div class="card"><div class="label">Current Basal</div><div class="value" style="font-size: 1.5rem;"><span id="u-basal">0.0</span><span class="unit">U/hr</span></div></div>
    </div>
    <hr>
    <div class="label" style="margin-bottom: 10px;">Bolus Delivery</div>
    <div class="grid">
      <input type="number" id="bolus-input" value="5.0" step="0.5" min="0.5">
      <button id="bolus-btn" onclick="sendBolus()">Deliver Bolus</button>
    </div>
    <div class="label" style="margin-top: 20px; margin-bottom: 10px;">System Reset</div>
    <button class="btn-danger" id="reset-btn" onclick="confirmReset()">Insert New Cartridge</button>
    <div id="alert-box" class="alert">RESERVOIR EMPTY</div>
  </div>

  <script>
    if (!!window.EventSource) {
      var source = new EventSource('/events');
      source.addEventListener('update', function(e) {
        var data = JSON.parse(e.data);
        document.getElementById("u-del").innerHTML = data.delivered.toFixed(1);
        document.getElementById("u-rem").innerHTML = data.remaining.toFixed(1);
        document.getElementById("u-cap").innerHTML = data.capacity.toFixed(0);
        document.getElementById("u-basal").innerHTML = data.basal.toFixed(1);
        
        var pct = (data.remaining / data.capacity) * 100;
        document.getElementById("pct").innerHTML = pct.toFixed(1);
        document.getElementById("progress-fill").style.width = Math.max(0, Math.min(100, pct)) + "%";

        var btn = document.getElementById("bolus-btn");
        var rstBtn = document.getElementById("reset-btn");
        var alertBox = document.getElementById("alert-box");

        if (data.rewinding) {
          btn.disabled = true; rstBtn.disabled = true;
          btn.innerText = "Rewinding...";
          alertBox.innerText = "REWINDING MOTOR...";
          alertBox.style.backgroundColor = "#e67e22"; 
          alertBox.style.display = "block";
        } else if (data.suspended) {
          btn.disabled = true; rstBtn.disabled = true;
          btn.innerText = "Suspended";
          alertBox.innerText = "DELIVERY SUSPENDED";
          alertBox.style.backgroundColor = "#9b59b6"; 
          alertBox.style.display = "block";
        } else if (data.empty) {
          btn.disabled = true; rstBtn.disabled = false;
          btn.innerText = "Empty"; 
          alertBox.innerText = "RESERVOIR EMPTY";
          alertBox.style.backgroundColor = "#e74c3c"; 
          alertBox.style.display = "block";
        } else if (data.pumping) {
          btn.disabled = true; rstBtn.disabled = true;
          btn.innerText = "Bolusing... (" + data.pending.toFixed(1) + ")"; 
          alertBox.style.display = "none";
        } else {
          btn.disabled = false; rstBtn.disabled = false;
          btn.innerText = "Deliver Bolus"; 
          alertBox.style.display = "none";
        }
      }, false);
    }
    
    // Uses the new JSON REST API
    function sendBolus() {
      var val = parseFloat(document.getElementById('bolus-input').value);
      fetch('/api/command/bolus', {
        method: 'POST',
        headers: { 'Content-Type': 'application/json' },
        body: JSON.stringify({ units: val, commandId: "web_bolus_" + Date.now() })
      }).then(res => { if (res.status !== 200) alert("Pump busy or suspended."); }).catch(err => console.error(err));
    }

    // Uses the new JSON REST API for Rewind
    function confirmReset() {
      if(confirm("This will physically rewind the motor to the 315U start position. Continue?")) {
        fetch('/api/command/reset', {
          method: 'POST',
          headers: { 'Content-Type': 'application/json' },
          body: JSON.stringify({ commandId: "web_reset_" + Date.now() })
        }).then(res => { if (res.status !== 200) alert("Pump busy."); }).catch(err => console.error(err));
      }
    }
  </script>
</body>
</html>
)rawliteral";

// ==========================================
// TIME & STATUS HELPERS
// ==========================================

unsigned long long getEpochMs() {
  struct timeval tv;
  gettimeofday(&tv, NULL);
  return (unsigned long long)(tv.tv_sec) * 1000 + (unsigned long long)(tv.tv_usec) / 1000;
}

String getDeviceStatus() {
  if (isRewinding) return "PRIMING";
  if (isSuspended) return "SUSPENDED";
  if (isPumping) return "DELIVERING_BOLUS";
  if (basalRateUph > 0 || isTempBasalActive) return "DELIVERING_BASAL";
  if (isReservoirEmpty) return "ERROR";
  return "IDLE";
}

float getActiveBasalRate() {
  return isTempBasalActive ? tempBasalRate : basalRateUph;
}

unsigned long getBasalIntervalMs() {
  float rate = getActiveBasalRate();
  if (rate <= 0.0) return 0xFFFFFFFF; 
  return (unsigned long)(3600000.0 / (rate / DOSE_INCREMENT));
}

// ==========================================
// HARDWARE UI (SH1106 OLED)
// ==========================================

void updateDisplay() {
  display.clearDisplay();
  display.setTextColor(SH110X_WHITE);

  // TOP BAR
  display.setTextSize(1);
  display.setCursor(0, 0);
  display.print("ST: ");
  display.print(getDeviceStatus());

  // WiFi Bars
  if (WiFi.status() == WL_CONNECTED) {
    int rssi = WiFi.RSSI();
    int bars = 0;
    if (rssi > -65) bars = 3;       
    else if (rssi > -75) bars = 2;  
    else if (rssi > -90) bars = 1;  

    if (bars >= 1) display.fillRect(116, 6, 2, 4, SH110X_WHITE);
    if (bars >= 2) display.fillRect(120, 4, 2, 6, SH110X_WHITE);
    if (bars >= 3) display.fillRect(124, 2, 2, 8, SH110X_WHITE);
  } else {
    display.setCursor(120, 0); display.print("X");
  }

  display.drawLine(0, 11, 128, 11, SH110X_WHITE);

  // MIDDLE
  display.setCursor(0, 18);
  display.setTextSize(2);
  display.print("Rem:");
  display.print(unitsRemaining, 1);
  display.print("U");

  // BOTTOM
  display.setTextSize(1);
  display.setCursor(0, 42);
  display.print("Basal: ");
  display.print(getActiveBasalRate(), 1);
  if (isTempBasalActive) display.print(" (TMP)");
  else display.print(" U/h");

  display.setCursor(0, 54);
  if (isSuspended) {
    display.print("*** SUSPENDED ***");
  } else if (isPumping) {
    display.print("Bolus: ");
    display.print(pendingUnits, 1);
    display.print(" U Left");
  } else {
    display.print("Last: ");
    display.print(lastBolusAmount, 1);
    display.print(" U");
  }

  display.display();
}

// ==========================================
// CORE LOGIC
// ==========================================

void saveStateToNVS() {
  preferences.putFloat("deliv", unitsDelivered);
  preferences.putFloat("rem", unitsRemaining);
  preferences.putFloat("basal", basalRateUph);
  preferences.putFloat("l_bolus", lastBolusAmount);
  preferences.putBool("empty", isReservoirEmpty);
  Serial.println("[NVS] System state saved.");
  stateDirty = false;
}

void loadStateFromNVS() {
  unitsDelivered = preferences.getFloat("deliv", 0.0);
  unitsRemaining = preferences.getFloat("rem", TOTAL_UNITS);
  basalRateUph = preferences.getFloat("basal", 0.0);
  lastBolusAmount = preferences.getFloat("l_bolus", 0.0);
  isReservoirEmpty = preferences.getBool("empty", false);
}

void updateClients() {
  String json = "{";
  json += "\"delivered\":" + String(unitsDelivered, 1) + ",";
  json += "\"remaining\":" + String(unitsRemaining, 1) + ",";
  json += "\"capacity\":" + String(TOTAL_UNITS, 1) + ",";
  json += "\"basal\":" + String(getActiveBasalRate(), 1) + ",";
  json += "\"empty\":" + String(isReservoirEmpty ? "true" : "false") + ",";
  json += "\"pumping\":" + String(isPumping ? "true" : "false") + ",";
  json += "\"rewinding\":" + String(isRewinding ? "true" : "false") + ",";
  json += "\"suspended\":" + String(isSuspended ? "true" : "false") + ",";
  json += "\"pending\":" + String(pendingUnits, 1);
  json += "}";
  events.send(json.c_str(), "update", millis());
  
  updateDisplay(); 
}

void triggerSingleTick(String type) {
  if (isReservoirEmpty || unitsRemaining <= 0 || isRewinding || isSuspended) {
    if (unitsRemaining <= 0 && !isReservoirEmpty) {
      isReservoirEmpty = true;
      isPumping = false;
      stateDirty = true;
      updateClients();
    }
    return;
  }

  unitsDelivered += DOSE_INCREMENT;
  unitsRemaining -= DOSE_INCREMENT;
  stateDirty = true; 

  // Physical Movement for Worm Gear (55ms)
  pumpServo.writeMicroseconds(SERVO_FORWARD); 
  delay(TICK_DURATION_MS);                        
  pumpServo.writeMicroseconds(SERVO_STOP); 

  Serial.printf("[%s] Tick delivered. Rem: %.1f U\n", type.c_str(), unitsRemaining);
  updateClients();
}

// ==========================================
// REST API ENDPOINTS
// ==========================================
void setupAPI() {
  
  // GET: /api/device/info
  server.on("/api/device/info", HTTP_GET, [](AsyncWebServerRequest *request){
    AsyncJsonResponse *response = new AsyncJsonResponse();
    JsonObject root = response->getRoot();
    root["serialNumber"] = "ESP32-PUMP-001";
    root["firmwareVersion"] = "1.0.0";
    root["hardwareVersion"] = "v1.0-WormDrive";
    root["deviceStatus"] = getDeviceStatus();
    root["batteryPercentage"] = 100; 
    root["reservoirVolume"] = unitsRemaining;
    root["activationStage"] = 5;
    root["communicationStatus"] = "CONNECTED";
    
    response->setLength();
    request->send(response);
  });

  // GET: /api/device/status
  server.on("/api/device/status", HTTP_GET, [](AsyncWebServerRequest *request){
    AsyncJsonResponse *response = new AsyncJsonResponse();
    JsonObject root = response->getRoot();
    root["deviceStatus"] = getDeviceStatus();
    root["batteryPercentage"] = 100;
    root["reservoirVolume"] = unitsRemaining;
    root["connectionState"] = "AUTHENTICATED_AND_READY";
    root["timestamp"] = getEpochMs();
    
    response->setLength();
    request->send(response);
  });

  // POST: /api/command/bolus
  AsyncCallbackJsonWebHandler* bolusHandler = new AsyncCallbackJsonWebHandler("/api/command/bolus", [](AsyncWebServerRequest *request, JsonVariant &json) {
    JsonObject jsonObj = json.as<JsonObject>();
    String cmdId = jsonObj["commandId"] | "unknown";
    
    AsyncJsonResponse *response = new AsyncJsonResponse();
    JsonObject root = response->getRoot();
    root["commandId"] = cmdId;
    root["timestamp"] = getEpochMs();

    if (isSuspended || isRewinding || isReservoirEmpty || isPumping) {
      request->send(409, "application/json", "{\"error\":\"Device busy or suspended\"}");
      return;
    }

    pendingUnits = jsonObj["units"].as<float>();
    lastBolusAmount = pendingUnits;
    isPumping = true;
    lastBolusTick = millis();
    stateDirty = true;

    root["status"] = "SUCCESS";
    JsonObject data = root.createNestedObject("data");
    data["unitsDelivered"] = pendingUnits; 
    data["startTime"] = getEpochMs();
    
    response->setLength();
    request->send(response);
    updateClients();
  });
  server.addHandler(bolusHandler);

  // POST: /api/command/temp-basal
  AsyncCallbackJsonWebHandler* tempBasalHandler = new AsyncCallbackJsonWebHandler("/api/command/temp-basal", [](AsyncWebServerRequest *request, JsonVariant &json) {
    JsonObject jsonObj = json.as<JsonObject>();
    float rate = jsonObj["rate"].as<float>();
    int durationMins = jsonObj["durationMinutes"].as<int>();
    
    isTempBasalActive = true;
    tempBasalRate = rate;
    tempBasalEndMillis = millis() + (durationMins * 60000);

    AsyncJsonResponse *response = new AsyncJsonResponse();
    JsonObject root = response->getRoot();
    root["commandId"] = jsonObj["commandId"] | "";
    root["status"] = "SUCCESS";
    root["timestamp"] = getEpochMs();
    JsonObject data = root.createNestedObject("data");
    data["rate"] = rate;
    data["durationMinutes"] = durationMins;
    
    response->setLength();
    request->send(response);
    updateClients();
  });
  server.addHandler(tempBasalHandler);

  // POST: /api/command/suspend
  AsyncCallbackJsonWebHandler* suspendHandler = new AsyncCallbackJsonWebHandler("/api/command/suspend", [](AsyncWebServerRequest *request, JsonVariant &json) {
    isSuspended = true;
    isPumping = false; // Cancel active boluses
    pendingUnits = 0.0;
    
    AsyncJsonResponse *response = new AsyncJsonResponse();
    JsonObject root = response->getRoot();
    root["commandId"] = json["commandId"] | "";
    root["status"] = "SUCCESS";
    root["timestamp"] = getEpochMs();
    root.createNestedObject("data")["deviceStatus"] = "SUSPENDED";
    
    response->setLength();
    request->send(response);
    updateClients();
  });
  server.addHandler(suspendHandler);

  // POST: /api/command/resume
  AsyncCallbackJsonWebHandler* resumeHandler = new AsyncCallbackJsonWebHandler("/api/command/resume", [](AsyncWebServerRequest *request, JsonVariant &json) {
    isSuspended = false;
    
    AsyncJsonResponse *response = new AsyncJsonResponse();
    JsonObject root = response->getRoot();
    root["commandId"] = json["commandId"] | "";
    root["status"] = "SUCCESS";
    root["timestamp"] = getEpochMs();
    root.createNestedObject("data")["deviceStatus"] = getDeviceStatus();
    
    response->setLength();
    request->send(response);
    updateClients();
  });
  server.addHandler(resumeHandler);

  // POST: /api/command/stop
  AsyncCallbackJsonWebHandler* stopHandler = new AsyncCallbackJsonWebHandler("/api/command/stop", [](AsyncWebServerRequest *request, JsonVariant &json) {
    isPumping = false;
    pendingUnits = 0.0;
    basalRateUph = 0.0;
    isTempBasalActive = false;
    stateDirty = true;
    
    AsyncJsonResponse *response = new AsyncJsonResponse();
    JsonObject root = response->getRoot();
    root["commandId"] = json["commandId"] | "";
    root["status"] = "SUCCESS";
    root["timestamp"] = getEpochMs();
    root.createNestedObject("data")["deviceStatus"] = "IDLE";
    
    response->setLength();
    request->send(response);
    updateClients();
  });
  server.addHandler(stopHandler);

  // POST: /api/command/beep
  AsyncCallbackJsonWebHandler* beepHandler = new AsyncCallbackJsonWebHandler("/api/command/beep", [](AsyncWebServerRequest *request, JsonVariant &json) {
    tone(BUZZER_PIN, 2000, 300); // Fire piezo buzzer
    
    AsyncJsonResponse *response = new AsyncJsonResponse();
    JsonObject root = response->getRoot();
    root["commandId"] = json["commandId"] | "";
    root["status"] = "SUCCESS";
    root["timestamp"] = getEpochMs();
    
    response->setLength();
    request->send(response);
  });
  server.addHandler(beepHandler);

  // POST: /api/command/reset (PHYSICAL REWIND LOGIC)
  AsyncCallbackJsonWebHandler* resetHandler = new AsyncCallbackJsonWebHandler("/api/command/reset", [](AsyncWebServerRequest *request, JsonVariant &json) {
    if (isPumping || isRewinding || isSuspended) {
      request->send(409, "application/json", "{\"error\":\"Device busy or suspended\"}");
      return;
    }

    // Physics: Calculate exact rewind time based on units delivered
    float totalTicks = unitsDelivered / DOSE_INCREMENT;
    rewindDuration = (unsigned long)(totalTicks * TICK_DURATION_MS);

    if (rewindDuration > 0) {
      isRewinding = true;
      rewindStartTime = millis();
      
      pumpServo.writeMicroseconds(SERVO_REVERSE); 
      Serial.printf("[PHYSICS] Rewinding worm gear for %lu ms...\n", rewindDuration);
    } else {
      unitsRemaining = TOTAL_UNITS;
      unitsDelivered = 0.0;
      isReservoirEmpty = false;
      saveStateToNVS();
    }

    AsyncJsonResponse *response = new AsyncJsonResponse();
    JsonObject root = response->getRoot();
    root["commandId"] = json["commandId"] | "reset_cmd";
    root["status"] = "SUCCESS";
    root["timestamp"] = getEpochMs();
    JsonObject data = root.createNestedObject("data");
    data["deviceStatus"] = "PRIMING";
    data["estimatedRewindDurationMs"] = rewindDuration;
    
    response->setLength();
    request->send(response);
    updateClients();
  });
  server.addHandler(resetHandler);
}

// ==========================================
// SETUP
// ==========================================
void setup() {
  Serial.begin(115200);

  // Load NVS State
  preferences.begin("pump-state", false);
  loadStateFromNVS();

  // Initialize OLED
  delay(250); 
  if(!display.begin(i2c_Address, true)) { 
    Serial.println(F("SH1106 allocation failed"));
    for(;;); 
  }
  display.clearDisplay();
  display.setTextColor(SH110X_WHITE);
  display.setCursor(0, 0); 
  display.print("Connecting WiFi..."); 
  display.display();

  // Initialize Hardware
  pumpServo.setPeriodHertz(50); 
  pumpServo.attach(SERVO_PIN); 
  pumpServo.writeMicroseconds(SERVO_STOP); 
  pinMode(BUTTON_PIN, INPUT_PULLUP);
  pinMode(BUZZER_PIN, OUTPUT);

  // Initialize WiFi
  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) { delay(500); Serial.print("."); }
  
  // Initialize NTP Time (For REST API Epoch ms)
  configTime(0, 0, "pool.ntp.org", "time.nist.gov");

  Serial.println("\nConnected! IP: " + WiFi.localIP().toString());
  updateDisplay(); 

  // Web Dashboard Route
  server.on("/", HTTP_GET, [](AsyncWebServerRequest *request){
    request->send_P(200, "text/html", index_html);
  });

  // Start API and SSE
  setupAPI();
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
  // 1. Temp Basal Expiration Check
  if (isTempBasalActive && millis() > tempBasalEndMillis) {
    isTempBasalActive = false;
    Serial.println("[SYSTEM] Temp Basal Finished.");
    updateClients();
  }

  // 2. REWIND STATE MACHINE (Physical Cartridge Reset)
  if (isRewinding) {
    if (millis() - rewindStartTime >= rewindDuration) {
      pumpServo.writeMicroseconds(SERVO_STOP);
      isRewinding = false;
      
      unitsRemaining = TOTAL_UNITS;
      unitsDelivered = 0.0;
      isReservoirEmpty = false;
      
      tone(BUZZER_PIN, 1000, 500); // Long beep to signal ready
      Serial.println("[SYSTEM] Mechanical Rewind Complete. System Ready.");
      updateClients();
      saveStateToNVS();
    }
  }

  // 3. MANUAL PRIME (Disabled during rewind/suspend)
  static int lastBtnState = HIGH;
  int btnState = digitalRead(BUTTON_PIN);
  if (lastBtnState == HIGH && btnState == LOW && !isPumping && !isRewinding && !isSuspended) {
    triggerSingleTick("PRIME");
    delay(200); 
  }
  lastBtnState = btnState;

  // 4. BOLUS STATE MACHINE
  if (isPumping && !isRewinding && !isSuspended) {
    if (millis() - lastBolusTick >= TICK_INTERVAL_MS) {
      if (pendingUnits > 0.01 && !isReservoirEmpty) {
        triggerSingleTick("BOLUS");
        pendingUnits -= DOSE_INCREMENT;
        lastBolusTick = millis(); 
      } 
      if (pendingUnits <= 0.01) {
        pendingUnits = 0.0;
        isPumping = false;
        tone(BUZZER_PIN, 1500, 150); // Beep on finish
        updateClients();
      }
    }
  }

  // 5. BASAL STATE MACHINE
  if (getActiveBasalRate() > 0.01 && !isReservoirEmpty && !isRewinding && !isSuspended) {
    unsigned long basalInterval = getBasalIntervalMs();
    if (millis() - lastBasalTick >= basalInterval) {
      if (!isPumping || (millis() - lastBolusTick > 200)) { 
        triggerSingleTick("BASAL");
        lastBasalTick = millis();
      }
    }
  }

  // 6. PERIODIC NVS SAVE
  if (stateDirty && (millis() - lastSaveTime >= SAVE_INTERVAL_MS)) {
    saveStateToNVS();
    lastSaveTime = millis();
  }

  // 7. KEEP-ALIVE UI UPDATES
  static unsigned long lastUpdate = 0;
  if (millis() - lastUpdate > 3000) { 
    updateClients();
    lastUpdate = millis();
  }
  
  delay(10); 
}