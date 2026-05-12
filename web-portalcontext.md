# Web Portal Port — Context

## Goal
Port the web portal from `iohomecontrol` (PlatformIO/Arduino) into 
`io-rts-esp32` (ESP-IDF, C++). Step by step, one testable piece at a time.

## Key files already written (in this conversation, not yet in project)
- WebServer.hpp / WebServer.cpp — drafted but not used yet
- We are starting from Step 1: serve index.html over HTTP

## What exists in the project
- IoRtsManager — holds mIoDevices map, mIoHome pointer
- mIoHome->OpenDevice(), CloseDevice(), StopDevice(), SetDevicePosition()
- MqttConfig, NetworkConfig — NVS-backed config classes
- LittleFS already mounted for device storage (partition label: check partitions.csv)
- CMakeLists.txt already has: littlefs, esp_wifi, mqtt, nvs_flash etc.
- esp_http_server NOT yet in CMakeLists.txt REQUIRES

# basis of the code and coding style
- the style of coding should be in line with the current coder
- Changes to the current code

## Step plan
1. Serve index.html (current step)
2. GET /api/devices
3. GET /api/remotes
4. POST /api/action (open/close/stop)
5. WebSocket for live updates
6. POST /api/command (rename, pair, delete)
7. MQTT settings endpoints
8. Download/upload backup