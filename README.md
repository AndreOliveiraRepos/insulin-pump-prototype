# ESP32 Insulin Pump Simulator

🚨 **DISCLAIMER: NOT A MEDICAL DEVICE** 🚨  
*This project is strictly for educational purposes, curiosity, and engineering prototyping. It is **NOT** a medical device, has **NOT** been approved by any regulatory body (FDA, EMA, etc.), and must **NEVER** be used to deliver insulin or any other medication/fluid to a human or animal. The creator(s) assume no liability for the misuse of this code or hardware and can not be held responsible for any malfunctions and occurencies using the available code and instructions. **PLEASE STAY SAFE** *

---

## 📖 Project Overview
This project simulates the electronic and software control systems of a lead screw-driven insulin pump. It uses an ESP32 microcontroller to drive a continuous rotation servo, simulating the precise pushing of a syringe plunger. It features a responsive, asynchronous web dashboard for remote control and monitoring.

## ✨ Features
* **Bolus Delivery:** Queue up specific doses (e.g., 5.00 Units) to be delivered continuously.
* **Basal Rates:** Configure a background drip rate (Units/hr) that runs independently of bolus doses.
* **State Persistence:** Uses ESP32 Non-Volatile Storage (NVS) to remember reservoir levels, basal rates, and total delivered units across power cycles and reboots.
* **Real-Time Web Dashboard:** Built with HTML/CSS/JS and Server-Sent Events (SSE) for zero-refresh live updates.
* **Hardware Trigger:** A physical push-button allows for manual "priming" of the line (0.05 Units per press).
* **Safety Limits:** Automatically locks the motor and web interface if the reservoir capacity reaches zero.

## 🛠️ Hardware Requirements
1. **ESP32 Development Board** (e.g., NodeMCU-32S)
2. **Continuous Rotation Servo** (e.g., 360° SG90). *Note: Standard 180° servos will not work with the current time-based pulse logic.*
3. **Push Button** (Normally Open)
4. **Jumper Wires & Breadboard**
5. *(Optional)* 3D Printed syringe/lead screw mechanism for physical simulation. (To be added in the future)

### Wiring Guide
| Component | ESP32 Pin | Notes |
| :--- | :--- | :--- |
| Servo Signal | `GPIO 18` | PWM Control |
| Servo VCC | `5V / VIN`| Ensure adequate power supply |
| Servo GND | `GND` | |


## 🚀 Setup & Installation

Clone this repository and open it in VS Code with the PlatformIO extension.

Open `main.cpp` (or your main sketch file) and update the WiFi credentials:

```C++
const char* ssid = "YOUR_WIFI_SSID";
const char* password = "YOUR_WIFI_PASSWORD";
```

(Optional) Tune the delay(150); inside the triggerSingleTick() function to calibrate the physical volume dispensed by your specific lead screw pitch.

Build and upload to your ESP32.

Open the Serial Monitor at 115200 baud.

Once connected to WiFi, the ESP32 will print its IP address. Navigate to this IP in any web browser on the same network.

## ⚙️ How it Works under the Hood

Time-based PWM: Because continuous rotation servos cannot go to a specific angle, the pump uses microsecond pulses (2000µs for forward, 1500µs for stop) for exactly 150ms to simulate a 0.05 Unit tick.

State Machine: The loop() function handles the timing for both Bolus intervals (1-second gaps) and Basal intervals (calculated via ms-per-hour) without using blocking delay() calls, keeping the web server fully responsive.

Dirty Flag Saving: To protect the ESP32's flash memory from wear, reservoir updates are not written to memory on every tick. Instead, a stateDirty flag is triggered, and a background timer safely commits changes to NVS every 30 seconds.

## Next steps

- Replace the continuous rotation servo
- Implement BLE interfaces
- Fix the Saving logic
- Implement physical interfaces and Screen
- Evaluate possible integration with AAPS