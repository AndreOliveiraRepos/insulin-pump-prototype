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