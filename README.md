# Singapore-Bus-Weather-ESP32-S3-RLCD
Bus and Weather information using Waveshare ESP32-S3 RLCD-4.2

Singapore Smart Bus Weather Arrival Display

An always-on, battery-powered display that shows live bus arrivals and local weather at a glance. Built on the Waveshare ESP32-S3-RLCD-4.2, it pulls arrival times from the LTA DataMall API and weather from the NEA via data.gov.sg.

Features
Live bus arrivals for a fixed set of services (53, 53M, 62, 113, 146) at two stops, with up to three upcoming buses per service
Local weather row with humidity, temperature and a vector-drawn icon for the current 2-hour forecast
Low-power design: CPU throttling, WiFi modem sleep and the panel's low-power refresh mode, with faster polling during weekday peak hours
Resilient networking: WiFi reconnect backoff, NTP sanity checks and tolerant JSON parsing
Glanceable layout: date, clock and battery level in the header; last-updated time in the footer
Hardware
Component	Details
Board	Waveshare ESP32-S3-RLCD-4.2 (ESP32-S3-WROOM-1, N16R8)
Display	4.2" reflective LCD, 400×300, ST7305
Power	3.7 V 18650 Li-ion via the onboard JST connector, or USB
<details> <summary>Pin mapping</summary>
Function	GPIO
SPI SCK	11
SPI MOSI	12
Display DC	5
Display CS	40
Display RST	41
Battery ADC	4 (3× voltage divider)
</details>
Requirements
Arduino IDE with arduino-esp32 v3.3.0 or newer
Libraries: U8g2 and ArduinoJson v7.x
An LTA DataMall API key
A data.gov.sg API key
A 2.4 GHz WiFi network
Getting Started

1. Clone the repository

bash
git clone https://github.com/<your-username>/<your-repo>.git

2. Add your credentials. Create secrets.h next to the sketch (it is git-ignored):

cpp
#pragma once

#define WIFI_SSID            "your-wifi-name"
#define WIFI_PASSWORD        "your-wifi-password"
#define LTA_API_KEY          "your-lta-datamall-key"
#define DATA_GOV_SG_API_KEY  "your-data-gov-sg-key"

3. Set your stops, buses and area. In the sketch, update the bus stop codes (the defaults are 63261 and 63269), the bus whitelist, and the NEA planning area used for the forecast (the default is Hougang).

4. Configure the Arduino IDE (Tools menu):

Setting	Value
Board	ESP32S3 Dev Module
USB CDC On Boot	Enabled (otherwise the Serial Monitor stays blank)
USB Mode	Hardware CDC and JTAG
Flash Size	16 MB
PSRAM	OPI PSRAM
Partition Scheme	16M Flash (3 MB APP / 9.9 MB FATFS)

5. Upload and monitor at 115200 baud. A healthy boot reports WiFi connected, bus data updated for both stops, and a valid weather fetch.

Display Layout
text
┌────────────────────────────────────────────────────────────┐
│ SAT, 24 OCT 2026          08:42                  [███] 97% │
├────────────────────────────────────────────────────────────┤
│ [ Opp Blk 232 ]                      [ Blk 232 ]           │
│                                                            │
│ 53       1    4    8            53       2    6   11       │
│ 53M      3    7   12            53M      4    9   14       │
│ 62       5   10   15            62       8   12    —       │
│ 113      —    —    —            113      —    —    —       │
│ 146      —    —    —            146      —    —    —       │
├────────────────────────────────────────────────────────────┤
│  77%                       30°C                         ☁  │
├────────────────────────────────────────────────────────────┤
│ Updated: 08:42 | 60s                                       │
└────────────────────────────────────────────────────────────┘

Arrival times are in minutes; "Arr" means under a minute, and "—" means no data.

How It Works
Data	Source	Notes
Bus arrivals	LTA DataMall v3/BusArrival	ETA is computed from the ISO 8601 EstimatedArrival against the ESP32 clock
Forecast	data.gov.sg /two-hr-forecast	Mapped to one of seven icons (sun, moon, partly cloudy, cloud, rain, thunderstorm, fog)
Temperature, humidity	data.gov.sg /air-temperature, /relative-humidity	A generic JSON reader extracts the first valid station reading

Polling: every 30 s on weekdays 07:00–08:59 (peak), otherwise every 60 s.

Power: the panel needs continuous power to hold its image, so deep sleep is not used. Expect roughly 28–42 hours on a 3400 mAh 18650 cell.

Project Structure
text
BusTime_WeatherPage_v2/
├── BusTime_Weather_v2.ino   Main sketch
├── secrets.h                Credentials (not committed)
├── README.md
└── docs/                    Full project documentation
Troubleshooting
Symptom	Fix
No serial output	Enable USB CDC On Boot, then press RESET after opening the monitor
Weather never loads	Check the data.gov.sg key and that the area name is a valid planning area (use Hougang, not Kovan)
HTTPS hangs or returns 401	Use WiFiClientSecure with setInsecure(), and pass header names without a trailing colon
Battery shows 0% or wrong value	Confirm 11 dB ADC attenuation and the 3× divider; calibrate against a multimeter
Negative or odd ETAs	Make sure the timezone is set with setenv("TZ", "SGT-8", 1) before NTP starts
