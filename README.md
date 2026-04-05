# ESP32-C5 Telemetry System

This project is a comprehensive telemetry system built for the ESP32-C5, featuring WiFi connectivity, MQTT data publishing, and SD card logging.

## Features
- **WiFi & MQTT:** Real-time data streaming to Ably MQTT broker.
- **Sensors:** 
  - Dual MPU6050 IMUs (Main and Steering).
  - ADS1115 External ADC for Throttle and Brakes.
  - Hall Effect Sensor for speed and distance tracking.
  - Internal ADC for Voltage and Current monitoring.
- **Data Logging:** Local storage on SD Card (SPI).
- **Time Sync:** SNTP synchronization for accurate timestamps.

## Configuration
- **Wheel Diameter:** 20 inches (Circumference: 1.5959m).
- **I2C Pins:** SDA (GPIO 4), SCL (GPIO 5).
- **JTAG:** Disabled in software to allow I2C on GPIO 4/5.

## Hardware Pin Mapping
| Peripheral | Pin |
|------------|-----|
| I2C SDA    | GPIO 4 |
| I2C SCL    | GPIO 5 |
| Hall Sensor| GPIO 7 |
| RGB LED    | GPIO 27|
| SD MOSI    | GPIO 10|
| SD MISO    | GPIO 9 |
| SD SCLK    | GPIO 8 |
| SD CS      | GPIO 0 |
| SD Detect  | GPIO 12|
