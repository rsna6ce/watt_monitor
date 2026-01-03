# watt_monitor

## Overview

This project is a system that uses the DOIT ESP32 DEVKIT V1 to measure power on up to 4 channels and automatically records the 1-minute average power values to a Google Spreadsheet.

- Hardware: ESP32 + SCT013-015 (non-invasive clamp-type current sensor)
- Firmware: `watt_monitor.ino`
- Data collection: `server_py/watt_monitor_server.py` (Python script)
- Data logging: Google Apps Script (`server_py/watt_monitor_post.gs`) stores 3 days of data (4320 records) in FIFO manner in a spreadsheet

## Hardware and Firmware (watt_monitor.ino)

### Supported Environment
- Board: DOIT ESP32 DEVKIT V1
- Arduino IDE 1.8.x
- ESP32 core version 2.0.17

### Sensor Connection
- Sensor used: SCT013-015 (15A / 1V output)
- Up to 4 channels simultaneously
- Analog input pins:
  - CH1 → GPIO32
  - CH2 → GPIO33
  - CH3 → GPIO34
  - CH4 → GPIO35

### Input Circuit (common to each channel)
The 3.3V supply is voltage-divided using two 4.7kΩ resistors, with the midpoint connected to the analog pin.  
A 10μF capacitor is connected in parallel between the analog pin and GND (for noise reduction).

### Wiring Diagram (ASCII Art)

@@@text
ESP32 DOIT DEVKIT V1
3.3V --- 4.7k --- (output1) -+- 4.7k -+- GND
                     |       |        |
                     |       +--10uF -+
                SCT-013-015
                     |
                  (output2)---GPIO(32,33,34,35) (ADC)

(Connect the same circuit in parallel for each channel)
@@@

### Usage
1. Open the repository folder in Arduino IDE
2. Select "ESP32 Dev Module" in the board manager
3. Upload the sketch
4. On first boot, use the Serial Monitor to set the SSID and password
5. Access the IP address shown in the Serial Monitor via a web browser (e.g., http://192.168.XXX.XXX)
6. After configuration, the ESP32 connects to the specified network and starts providing the following JSON at the `/api/power` endpoint

`latest` is the instantaneous value, `avg` is the 1-minute average value

@@@json
{
  "latest": [ch1, ch2, ch3, ch4],
  "avg": [ch1_avg, ch2_avg, ch3_avg, ch4_avg]
}
@@@

## Server-side Script (server_py/watt_monitor_server.py)

A Python script that retrieves the 1-minute average power from the ESP32 and POSTs it to Google Apps Script.  
It runs as a one-shot process, so it is executed periodically via crontab.

### Execution Method
- Python 3.x (standard libraries only, no external dependencies)
- Place `param.json` in the same directory as the script

@@@bash
cd server_py
python3 watt_monitor_server.py   # Test execution
@@@

### param.json Example

@@@json
{
    "devices": [
        {"name": "device1", "ipaddr":"192.168.1.53", "num_channels":1},
        {"name": "device2", "ipaddr":"192.168.1.54", "num_channels":2},
        {"name": "device3", "ipaddr":"192.168.1.55", "num_channels":2},
        {"name": "device4", "ipaddr":"192.168.1.56", "num_channels":2}
    ],
    "post_url": "https://script.google.com/macros/s/xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx/exec"
}
@@@

### Example: Run Every Minute via crontab

@@@bash
crontab -e
@@@

Add the following line:

@@@cron
* * * * * /usr/bin/python3 /home/youruser/work/watt_monitor/server_py/watt_monitor_server.py
@@@

## Google Apps Script (server_py/watt_monitor_post.gs)

Records data to a spreadsheet (with header row; data starts from row 2 and is stored in FIFO order).

### Setup Instructions
1. Create a new Google Spreadsheet
2. Rename the sheet to "data"
3. Go to Extensions → Apps Script
4. Paste the entire contents of `watt_monitor_post.gs` and save
5. Deploy → New deployment → Select type: Web app
   - Execute as: Me
   - Who has access: Anyone
6. Copy the displayed Web app URL and set it as `post_url` in `param.json`

With this setup, power data is automatically accumulated in the spreadsheet every minute!

## License
MIT License (see LICENSE for details)

Questions and improvement suggestions are welcome via Issues or Pull Requests!
