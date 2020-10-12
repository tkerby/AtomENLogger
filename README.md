# BLE Exposure Notification Logger

Builds on the excellent work of Kaspar Metz: https://github.com/kmetz/BLEExposureNotificationBeeper adding functionality for logging of exposure notification beacons to a file including GPS coordinates and timestamps. 

Where countries provide open access to the Temporary Exposure Keys, this data can be compared with the published keys to understand more details about where you were close to someone infected with COVID-19, allowing you to make a more accurate personal assessment of risk (e.g. ignore a match when alone in your car in a traffic jam).

With some data conversion, the https://github.com/mh-/diagnosis-keys application can be used to provide exposure information.

The device uses the M5Stack Atom GPS hardware https://docs.m5stack.com/#/en/atom/atomicgps to provide a fully self contained unit. The GPS used includes an RTC delivering accurate timing even when outside of GPS signal.

The data collected is written to an SD card as JSON files allowing for conversion into csv or RamBLE style SQLite databases for visualisation.

The Atom LED will flash for each exposure notification packet seen. The colours are
- Red: First time a device is seen
- Yellow: Previously seen device with no GPS signal (will not save position data)
- Blue: Previously seen device with valid GPS signal (will save position data)

## Legal
Notifiers (nearby devices with a warning app) are remembered for 20 minutes so it only flashes Red for newly detected ones. Note that the IDs will change every 15 minutes or so for privacy reasons, triggering new red flashes.

Please check the legal status of Bluetooth Scanning and use of Exposure Notification data within your local jurisdiction. This application is provided as a proof of concept only and any use or misuse is the full responsibility of the end user. The author takes no responsibility and accepts no liability for the use of this software.

### Design Considerations
The application only stores bluetooth data from exposure notification packets which are anonymous by design and utilise rolling bluetooth MAC addresses and RPI codes to protect the privacy of users. No other bluetooth data is stored to avoid capture for instance of identifying device names or other nearby hardware. The EN packets are sent by bluetooth advertisement which is intended for general reception in the Bluetooth Protocols. No connection is negotiated with external devices and passive scanning is used to prevent requests for additional scan data.

## Build your own
### You need
1. An M5Stack Atom GPS kit https://m5stack.com/products/atom-gps-kit-m8030-kt
2. An SD Card
3. A USB powerbank

### Flashing the ESP32
If you don't have [PlatfomIO](https://platformio.org/platformio-ide) installed, you can flash instal libraries and use the Arduino IDE.

#### Using PlatfomIO
- Simply open the project and upload.
- Or via command line: `platformio run -t upload`

