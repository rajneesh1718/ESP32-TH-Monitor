# ESP32-TH-Monitor

ESP32-based real-time Temperature & Humidity Monitoring System using a DHT11 sensor, FreeRTOS, Wi-Fi, REST API, WebSocket, and a responsive web dashboard.

## Overview

ESP32-TH-Monitor is an embedded IoT monitoring system designed to acquire, process, and visualize environmental temperature and humidity data in real time.

The system uses FreeRTOS-based tasks to separate sensor acquisition, data processing, monitoring, and network management.

## Features

- DHT11 temperature and humidity sensing
- 2-second sampling interval
- Sensor data validation
- Calibration offsets
- Moving-average filtering
- Exponential Moving Average (EMA)
- Outlier detection
- Temperature and humidity rate-of-change calculation
- Min / Max / Average statistics
- FreeRTOS task-based architecture
- FreeRTOS queues and mutex synchronization
- Wi-Fi connectivity with automatic reconnection
- mDNS hostname support
- REST API
- WebSocket real-time data streaming
- Responsive web dashboard
- NTP time synchronization
- Configurable temperature/humidity alerts
- Alert hysteresis and cooldown
- Persistent configuration using ESP32 NVS
- Diagnostic API
- OTA firmware update intentionally excluded

## System Architecture

```text
                 ┌─────────────┐
                 │    DHT11    │
                 └──────┬──────┘
                        │
                        ▼
                 ┌─────────────┐
                 │ Sensor Task │
                 └──────┬──────┘
                        │
                        ▼
                 ┌─────────────┐
                 │  Raw Queue  │
                 └──────┬──────┘
                        │
                        ▼
              ┌───────────────────┐
              │ Processing Task   │
              │                   │
              │ Validation        │
              │ Calibration       │
              │ Moving Average    │
              │ EMA Filtering     │
              │ Outlier Detection │
              │ Rate Calculation  │
              └─────────┬─────────┘
                        │
                        ▼
                 ┌─────────────┐
                 │ Data State  │
                 └──────┬──────┘
                        │
          ┌─────────────┼─────────────┐
          ▼             ▼             ▼
     REST API       WebSocket      Alerts
          │             │
          └──────┬──────┘
                 ▼
          Web Dashboard
