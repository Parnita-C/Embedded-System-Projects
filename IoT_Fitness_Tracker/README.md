# Smart Interactive IoT Fitness Trainer
### CC3200 LaunchXL — CC3200SDK_1.4.0
**Authors:** Parnita Chowtoori, Selena Phu | EEC 172 - A03

---

## Project Overview

A standalone embedded fitness tracker that:
- Counts **push-up reps** using an IR proximity sensor (state-machine detection)
- Counts **running steps** using the onboard BMA222 accelerometer (peak detection)
- Displays real-time feedback on a 128×128 SSD1351 OLED
- Uploads session data to a cloud web service via Wi-Fi (HTTP POST)

---

## Hardware Connections

### Pin Summary (CC3200 LaunchXL headers)

| Signal | Physical Pin | Dev Pin | Header | Notes |
|---|---|---|---|---|
| I2C SCL | Pin 1 | 1 | P1 | Accelerometer (BMA222 onboard) |
| I2C SDA | Pin 2 | 2 | P1 | Accelerometer (BMA222 onboard) |
| SPI CLK | Pin 5 | 5 | P1 | OLED clock |
| SPI MOSI (DOUT) | Pin 7 | 7 | P2 | OLED data |
| SPI MISO (DIN) | Pin 6 | 6 | P2 | OLED (unused, configured) |
| SPI CS | Pin 8 | 8 | P2 | OLED chip select |
| OLED D/C | Pin 61 | 61 | P1 | OLED data/command GPIO |
| OLED RESET | Pin 62 | 62 | P1 | OLED reset GPIO |
| IR Sensor Output | Pin 18 | 28 | P1 | Digital IN, active LOW |
| SW2 (Mode) | Pin 4 | 4 | P1 | Onboard button (pull-up) |
| SW3 (Start) | Pin 16 | 55 | P2 | Onboard button (pull-up) |
| 3.3V | P1 top | — | P1 | Power for OLED + IR sensor |
| GND | P3 top | — | P3 | Common ground |

### OLED Wiring (SSD1351 / Adafruit 128×128)
```
OLED Pin  →  CC3200
VCC       →  3.3V
GND       →  GND
SCL/CLK   →  Pin 5  (SPI CLK)
SDA/MOSI  →  Pin 7  (SPI DOUT)
CS        →  Pin 8  (SPI CS)
D/C       →  Pin 61 (GPIO)
RST       →  Pin 62 (GPIO)
```

### IR Proximity Sensor Wiring
```
IR Module  →  CC3200
VCC        →  3.3V
GND        →  GND
OUT        →  Pin 15 (GPIO 22, active LOW)
```
Place the IR sensor pointing downward (for push-ups: sensor on floor,
detects when your chest approaches). Adjust the onboard potentiometer
for ~10–20 cm trigger distance.

---

## Software Architecture

```
main.c              State machine + timer ISR + button handling
├── pin_mux_config  PINMUX setup for all peripherals
├── accelerometer   BMA222 I2C driver + peak-detection step counter
├── ir_sensor       GPIO IR driver + push-up state machine
├── oled            SSD1351 SPI driver + 6×8 font renderer
└── wifi_upload     SimpleLink Wi-Fi + HTTP POST to web service
```

### State Machine
```
IDLE ──[SW3]──► MODE_SELECT ──[SW3 confirm]──► PUSHUP_TRACK ──[SW2]──►┐
                    ↑                      └──► RUNNING_TRACK ──[SW2]──►┤
                    │                                                    ↓
                    └───────────────────────── IDLE ◄── UPLOAD ◄── FEEDBACK
```

---

## CCS Project Setup

### 1. Import Project
```
File > Import > Code Composer Studio > Existing CCS Eclipse Projects
Select directory: <this folder>
```

### 2. Set SDK_ROOT variable
```
Window > Preferences > Code Composer Studio > Build > Variables
Add: SDK_ROOT = C:\ti\CC3200SDK_1.4.0\cc3200-sdk   (adjust to your path)
```

### 3. Include Paths  
Right-click project → Properties → Build → ARM Compiler → Include Options:
```
${SDK_ROOT}/driverlib
${SDK_ROOT}/inc
${SDK_ROOT}/simplelink/include
${SDK_ROOT}/oslib
${PROJECT_LOC}/include
```

### 4. Linker Libraries  
Right-click project → Properties → Build → ARM Linker → File Search Path:
```
${SDK_ROOT}/driverlib/gcc/exe/libdriver.a
${SDK_ROOT}/simplelink/gcc/exe/libsimplelink.a
```
Linker command file:
```
${SDK_ROOT}/tools/gcc_scripts/cc3200v1p32.lds
```

### 5. Configure Wi-Fi credentials  
Edit `src/wifi_upload.c`:
```c
#define WIFI_SSID     "YourNetworkName"
#define WIFI_PASSWORD "YourPassword"
#define SERVER_HOST   "your-server.com"   // or IP address
#define SERVER_PATH   "/api/workout"
```

### 6. Build & Flash
- Build: **Project > Build All** (Ctrl+B)
- Connect CC3200 LaunchXL via USB (SOP[1] jumper = 01 for flash mode)
- Flash: **Run > Debug** → uses XDS110 JTAG or Uniflash

---

## Usage

| Action | Button | State |
|---|---|---|
| Start session | SW3 | IDLE |
| Cycle mode (Push-up / Running) | SW2 | MODE_SELECT |
| Stop session | SW3 | TRACKING |

After stopping, the OLED shows your count + time, uploads to Wi-Fi,
then returns to IDLE after 3 seconds.

---

## HTTP Upload Format

The device POSTs JSON to your server endpoint:
```json
// Push-up session
{ "mode": "pushup", "reps": 15 }

// Running session
{ "mode": "running", "steps": 342 }
```

Your server should respond with HTTP 200 to confirm receipt.
A simple Flask server example:
```python
from flask import Flask, request
app = Flask(__name__)

@app.route('/api/workout', methods=['POST'])
def workout():
    print(request.json)
    return "OK", 200

app.run(host='0.0.0.0', port=80)
```

---

## Push-up Detection Algorithm

The IR sensor outputs digital LOW when an object is within range.  
A full rep is counted on the state transition **DOWN → TOP**:

```
Position:  TOP  →  DOWN  →  TOP  = 1 rep
IR output: HIGH    LOW      HIGH
```

A 3-sample debounce (~60 ms) prevents false triggers from vibration.

## Step Detection Algorithm

The BMA222 Z-axis is sampled at 50 Hz (every 20 ms).  
A step is detected when:
1. Z acceleration rises above **250 mg** threshold (rising edge)
2. Z acceleration then falls (falling edge = peak passed)
3. At least **250 ms** has elapsed since the last step (debounce)

This is a zero-crossing peak detector suitable for walking and running.

---

## File Structure

```
fitness_trainer/
├── .project            CCS project descriptor
├── .ccsproject         CCS tool settings + setup instructions
├── README.md           This file
├── include/
│   ├── pin_mux_config.h
│   ├── accelerometer.h
│   ├── ir_sensor.h
│   ├── oled.h
│   └── wifi_upload.h
└── src/
    ├── main.c           State machine, timer ISR, entry point
    ├── pin_mux_config.c PINMUX for I2C, SPI, GPIO
    ├── accelerometer.c  BMA222 driver + step detection
    ├── ir_sensor.c      IR driver + push-up rep counting
    ├── oled.c           SSD1351 SPI driver + font + screen layouts
    └── wifi_upload.c    SimpleLink Wi-Fi + HTTP POST
```
