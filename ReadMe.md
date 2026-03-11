# Smart Electricity Meter

## Overview
A project for monitoring electricity usegae/bill and controlling home electrical aplliances remotely

## Features
- Data monitoring and Automation
- User-friendly dashboard (By BLYNK)
- Automated alerts for Electricity Bill Payments

## Hardware Components
- **ESP8266**: WiFi-enabled microcontroller for IoT connectivity
- **Raspberry Pi Pico**: Real-time data acquisition and processing

## Firmware Structure
```
firmware - ESP8266/
├── platformio.ini
├── src/
│   └── main.cpp
└── .gitignore

firmware - RaspberryPI Pico/
├── platformio.ini
├── src/
│   └── main.cpp
└── .gitignore
```

## IoT Integration
This project uses **Blynk** for cloud connectivity and real-time monitoring and automation via mobile app.

## Setup
1. Configure PlatformIO for ESP8266 and Raspberry Pi Pico (auto in case of opening both boards dir in sepearate window)
2. Update Blynk credentials and WiFi Credentials in ESP8266 firmware
3. Update Receiver Mobile no. in Pico W firmware in respiective field { char Mobile_Num[] = "+91XXXXXXXXXX"; } 
4. Upload firmware to respective microcontrollers
4. Setup BlYNK Console as Described below
5. Connect device to the main circuit and network.

## BLYNK Console Setup
### Configure datastream for virtaul-pins same as shown below:
![Blynk Datastream](/resources/Datastream.png)
### Configure dahsboard layout for buttons and labels and link respective virtual-pins as shown below:
![Blynk Dashboard Layoput](/resources/Dashboard.png)



## Development
- **Language**: C++
- **Framework**: PlatformIO

## Developer
**Gaurav@ElectroVilla**
