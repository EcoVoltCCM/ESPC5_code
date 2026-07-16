# ESP32-C5 Telemetry System

This project is a telemetry and data acquisition system for a vehicle, built around the ESP32-C5 microcontroller. It reads from multiple sensors, logs data locally to an SD card, and transmits it via WiFi over MQTT.

## Recent Updates

- **Efficiency Metrics**: Added `acc_eff_km_kwh` (accumulated efficiency) and `inst_eff_km_kwh` (instantaneous efficiency) to both the SD card and the MQTT JSON payload. Both metrics seamlessly support regenerative braking (negative power).
- **Calibration Removed**: The startup current calibration procedure (which averaged 1000 samples) was completely removed to speed up boot times and stream raw values directly.

## Libraries Overview

The project is structured into various libraries located in the `main/libs` directory:

- **adc_reader**: Reads internal ADC for voltage and current sensing (with peak/average tracking and energy integration).
- **ads1115**: Driver for the external ADS1115 ADC (used for throttle and brake pedals).
- **config**: Centralized configuration parameters (WiFi credentials, MQTT keys, pin definitions, etc.).
- **data_types**: Defines the shared structures used to pass raw sensor data across tasks.
- **gps**: Handles UART communication with the GPS module to parse position and speed (NMEA).
- **hall_sensor**: Interrupt-driven sensor library to calculate speed and distance using a Hall effect sensor on the wheels.
- **led_indicator**: Manages the RGB LED to provide visual status feedback (WiFi, SD card, errors).
- **mpu6050**: Driver for the MPU6050 IMU to get acceleration and gyroscope data.
- **mqtt_client**: Manages the MQTT connection to Ably and publishes telemetry data securely.
- **sd_card**: Handles SPI communication, FAT filesystem mounting, and writing telemetry data to CSV files.
- **telemetry_data**: Defines the `TelemetryData` class that encapsulates all sensor readings and handles formatting to JSON/CSV.
- **telemetry_system**: The core orchestrator that initializes hardware, spawns the sensor and sink tasks, and manages the data queue.
- **wifi_manager**: Handles WiFi initialization, connection events, and band configuration (e.g. 2.4GHz / 5GHz).

## Telemetry Variables

The following data is packaged and sent via MQTT (as JSON) and saved to the SD Card (as CSV):

| Variable Name | Type | Description |
|---|---|---|
| `timestamp` | `char[32]` | ISO 8601 Timestamp |
| `message_id` | `int` | Sequential counter of the message |
| `uptime_seconds` | `float` | ESP32 uptime in seconds |
| `speed_ms` | `float` | Speed from GPS in m/s |
| `hall_speed_ms` | `float` | Speed from Hall sensor in m/s |
| `latitude` | `double` | GPS Latitude |
| `longitude` | `double` | GPS Longitude |
| `altitude` | `float` | GPS Altitude |
| `voltage_v` | `float` | Battery/System voltage |
| `current_a` | `float` | Average current consumption |
| `max_current_a` | `float` | Peak current in the measurement window |
| `avg_power_w` | `float` | Average power consumption (Watts) |
| `energy_j` | `float` | Cumulative energy used (Joules) |
| `distance_m` | `float` | Cumulative distance traveled (Meters) |
| `throttle_pct` | `float` | Throttle position percentage (0-100) |
| `brake_pct` | `float` | Main brake position percentage (0-100) |
| `brake2_pct` | `float` | Secondary brake position percentage (0-100) |
| `accel_x, accel_y, accel_z` | `float` | Acceleration from main IMU |
| `gyro_x, gyro_y, gyro_z` | `float` | Gyroscope from main IMU |
| `g_lat` | `float` | Lateral acceleration (G-force) |
| `g_long` | `float` | Longitudinal acceleration (G-force) |
| `acc_eff_km_kwh` | `float` | Accumulated efficiency (km/kWh) |
| `inst_eff_km_kwh` | `float` | Instantaneous efficiency (km/kWh) |
| `vehicle_heading` | `float` | Integrated heading from Z-gyro |
| `total_acceleration` | `float` | Magnitude of the acceleration vector |
| `steering_accel_x, _y, _z` | `float` | Acceleration from steering IMU |
| `steering_gyro_x, _y, _z` | `float` | Gyroscope from steering IMU |
