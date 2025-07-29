# TX Passive Thermal GCT - Master ESP32

## Project Overview
This is the master ESP32 device for the Thermal Ground Contact Temperature (GCT) monitoring system. It collects temperature data from up to 4 servant ESP32 devices via ESP-NOW protocol, logs data to SD card, and provides real-time display on LCD.

## Credits and Attribution
**Original Code Base**: This project is based on the initial implementation by MoritzNelle:
- Original TX repository: https://github.com/MoritzNelle/TX_Passive_Thermal_GCT
- Original RX repository: https://github.com/MoritzNelle/RX_Passive_Thermal_GCT

**Development**: Enhanced and extended by **Josias Kern** using **GitHub Copilot** (GPT-4 model) for:
- WiFi connectivity and NTP time synchronization
- System robustness improvements (watchdog timers, error handling)
- Automated build configurations and project infrastructure
- Comprehensive documentation and setup automation

## Features
- **ESP-NOW Communication**: Wireless communication with servant devices
- **WiFi & NTP Time Sync**: Automatic time synchronization on startup
- **SD Card Logging**: Automatic data logging with CSV format
- **LCD Display**: Real-time temperature and status display
- **Button Control**: Manual logging start/stop
- **Status LED**: Visual system status indication
- **Watchdog Timer**: System reliability and auto-recovery
- **Robust Error Handling**: Graceful handling of component failures

## Hardware Requirements
- ESP32 development board (NodeMCU-32S compatible)
- 20x4 I2C LCD display (address 0x27)
- SD card module
- DS3231 RTC module
- NeoPixel LED (WS2812B)
- Push button
- MicroSD card

## Pin Configuration
| Component | Pin | Notes |
|-----------|-----|-------|
| SD Card CS | GPIO 5 | SPI Chip Select |
| Status LED | GPIO 4 | NeoPixel Data |
| Button | GPIO 0 | Active LOW |
| LCD SDA | GPIO 21 | I2C Data |
| LCD SCL | GPIO 22 | I2C Clock |
| RTC SDA | GPIO 21 | I2C Data (shared) |
| RTC SCL | GPIO 22 | I2C Clock (shared) |

## Configuration
Edit `include/config.h` to customize:
- WiFi credentials
- Servant device MAC addresses
- Timing intervals
- Hardware pin assignments

## Building and Uploading

### Standard Build
```bash
pio run -e tx-master-esp32
pio run -e tx-master-esp32 --target upload
```

### Debug Build
```bash
pio run -e tx-master-debug
pio run -e tx-master-debug --target upload
```

## Operation
1. **Startup**: Device initializes all components and attempts WiFi/NTP sync
2. **Connection Check**: Continuously monitors servant device connections
3. **Temperature Display**: Shows live temperatures from all connected devices
4. **Manual Logging**: Press button to start/stop data logging
5. **Automatic Logging**: Data logged at configured intervals when enabled

## Status LED Indicators
- **Off**: System ready
- **Yellow Solid**: Initializing
- **Green Blink**: All devices connected, not logging
- **Green Solid**: All devices connected, logging active
- **Yellow Blink**: Some devices disconnected
- **Red Blink**: System error
- **Red Solid**: Critical error

## File Structure
```
TX_Passive_Thermal_GCT/
├── include/
│   └── config.h           # Configuration header
├── src/
│   └── main.cpp          # Main application code
├── platformio.ini        # PlatformIO configuration
└── README.md            # This file
```

## Troubleshooting
- **No WiFi**: Check credentials in config.h, device continues without NTP
- **SD Card Error**: Check card format (FAT32), connection, and card health
- **No Servant Connection**: Verify MAC addresses and servant device status
- **RTC Error**: Check I2C connections and RTC battery

## Data Format
CSV data logged to `/data_master.csv`:
```
timestamp,target_no,sensor_no,temperature
2025-07-29 14:30:15,1,1,23.5
2025-07-29 14:30:15,1,2,24.1
...
```

## Version History
- **v1.2.0**: Added WiFi/NTP sync, improved error handling, watchdog timer
- **v1.1.0**: Enhanced button debouncing, connection state tracking
- **v1.0.0**: Initial release with basic functionality
