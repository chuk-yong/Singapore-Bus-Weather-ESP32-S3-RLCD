#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <time.h>
#include <U8g2lib.h>
#include <SPI.h>
#include "secrets.h"

// ==========================================
// 1. CONFIGURATION & PIN DEFINITIONS
// ==========================================
const char* LTA_BASE_URL = "https://datamall2.mytransport.sg/ltaodataservice/v3/BusArrival";
const char* NTP_SERVER = "pool.ntp.org";

// Waveshare ESP32-S3-RLCD-4.2 Pin Definitions
#define RLCD_SCK 11
#define RLCD_MOSI 12
#define RLCD_DC 5
#define RLCD_CS 40
#define RLCD_RST 41
#define BATT_ADC_PIN 4

// U8g2 Constructor for 4.2" RLCD (400x300)
U8G2_ST7305_300X400_F_4W_HW_SPI u8g2(U8G2_R1, RLCD_CS, RLCD_DC, RLCD_RST);

// Target Buses (Strict Order)
const char* TARGET_BUSES[] = {"53", "53M", "62", "113", "146"};
const int NUM_TARGET_BUSES = 5;

// ==========================================
// 2. DATA STRUCTURES & GLOBAL STATE
// ==========================================
unsigned long lastDataUpdate = 0;
unsigned long currentUpdateInterval = 60000;
bool dataNeedsRedraw = true;
bool wifiConnected = false;

struct BusService {
  String serviceNo;
  int eta1;
  int eta2;
  int eta3;
};

BusService busDataStop1[NUM_TARGET_BUSES];
BusService busDataStop2[NUM_TARGET_BUSES];

struct WeatherData {
  bool isValid;
  int tempC;
  int humidity;
  String forecast;
} weather;

// ==========================================
// 3. FORWARD DECLARATIONS
// ==========================================
void drawBusPage();
void drawHeader();
void drawWeather();
void drawFooter();
void drawColumn(int x, int y, const char* label, BusService* busArray);
void fetchAllData();
void fetchBusData(const char* stopCode, BusService* busArray);
void fetchWeatherData();
void calculateUpdateInterval();
int calculateETA(const char* isoTime);
uint8_t getBatteryPercent();

// Vector weather icon functions
enum WeatherIcon { WI_SUN, WI_MOON, WI_PARTLY, WI_CLOUD, WI_RAIN, WI_THUNDER, WI_FOG };
WeatherIcon classifyWeather(const char* f);
void drawWeatherIcon(int x, int y, const char* forecast);

// ==========================================
// 4. SETUP & LOOP
// ==========================================
void setup() {
  Serial.begin(115200);
  setCpuFrequencyMhz(80);
  WiFi.setSleep(true);
  
  // CRITICAL FIX: Set Timezone to SGT (UTC+8)
  setenv("TZ", "SGT-8", 1);
  tzset();

  // Initialize Display
  SPI.begin(RLCD_SCK, -1, RLCD_MOSI, RLCD_CS);
  u8g2.begin();
  u8g2.setBusClock(24000000);
  Serial.println("Display Initialized.");

  // Initialize Weather struct
  weather.isValid = false;
  weather.tempC = 0;
  weather.humidity = 0;
  weather.forecast = "No Data";

  // Connect to WiFi
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  Serial.print("Connecting to WiFi");
  int attempts = 0;
  while (WiFi.status() != WL_CONNECTED && attempts < 40) {
    delay(500);
    Serial.print(".");
    attempts++;
  }
  
  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("\nWiFi Connected!");
    wifiConnected = true;
    configTime(8 * 3600, 0, NTP_SERVER);
    fetchAllData();
  } else {
    Serial.println("\nWiFi Failed. Will retry in loop.");
  }
}

void loop() {
  if (WiFi.status() != WL_CONNECTED) {
    wifiConnected = false;
    WiFi.reconnect();
    delay(2000);
  } else if (!wifiConnected) {
    wifiConnected = true;
    Serial.println("WiFi Reconnected!");
    configTime(8 * 3600, 0, NTP_SERVER);
  }

  calculateUpdateInterval();
  unsigned long currentMillis = millis();
  
  if (currentMillis - lastDataUpdate >= currentUpdateInterval) {
    fetchAllData();
    lastDataUpdate = currentMillis;
    dataNeedsRedraw = true;
  }

  if (dataNeedsRedraw) {
    u8g2.sendF("c", 0x38);
    drawBusPage();
    u8g2.sendF("c", 0x39);
    dataNeedsRedraw = false;
  }
  
  delay(50);
}

// ==========================================
// 5. DATA FETCHING & PARSING
// ==========================================
void calculateUpdateInterval() {
  struct tm timeinfo;
  if (getLocalTime(&timeinfo)) {
    int hour = timeinfo.tm_hour;
    int wday = timeinfo.tm_wday;
    bool isPeak = (wday >= 1 && wday <= 5) && (hour >= 7 && hour < 9);
    currentUpdateInterval = isPeak ? 30000 : 60000;
  }
}

void fetchAllData() {
  if (!wifiConnected) return;
  fetchBusData("63261", busDataStop1);
  fetchBusData("63269", busDataStop2);
  fetchWeatherData();
}

void fetchBusData(const char* stopCode, BusService* busArray) {
  for (int i = 0; i < NUM_TARGET_BUSES; i++) {
    busArray[i].serviceNo = TARGET_BUSES[i];
    busArray[i].eta1 = -1;
    busArray[i].eta2 = -1;
    busArray[i].eta3 = -1;
  }

  HTTPClient http;
  char url[150];
  snprintf(url, sizeof(url), "%s?BusStopCode=%s", LTA_BASE_URL, stopCode);
  http.begin(url);
  http.addHeader("AccountKey", LTA_API_KEY);
  http.addHeader("accept", "application/json");
  http.setTimeout(5000);

  int httpCode = http.GET();
  if (httpCode == HTTP_CODE_OK) {
    String payload = http.getString();
    JsonDocument doc;
    if (deserializeJson(doc, payload) == DeserializationError::Ok) {
      JsonArray services;
      if (doc.containsKey("Services")) {
        services = doc["Services"];
      } else if (doc.containsKey("value") && doc["value"].size() > 0) {
        services = doc["value"][0]["Services"];
      }

      if (!services.isNull()) {
        for (size_t i = 0; i < services.size(); i++) {
          String svc = services[i]["ServiceNo"].as<String>();
          for (int t = 0; t < NUM_TARGET_BUSES; t++) {
            if (svc == TARGET_BUSES[t]) {
              const char* eta1Str = services[i]["NextBus"]["EstimatedArrival"].as<const char*>();
              const char* eta2Str = services[i]["NextBus2"]["EstimatedArrival"].as<const char*>();
              const char* eta3Str = services[i]["NextBus3"]["EstimatedArrival"].as<const char*>();
              
              busArray[t].eta1 = calculateETA(eta1Str);
              busArray[t].eta2 = calculateETA(eta2Str);
              busArray[t].eta3 = calculateETA(eta3Str);
              break;
            }
          }
        }
        Serial.printf("Bus data updated for stop %s\n", stopCode);
      }
    }
  }
  http.end();
}

// ============================================================
// GENERIC READING EXTRACTOR - ignores station metadata entirely
// ============================================================
static bool extractReading(JsonVariant node, int &outValue, int depth = 0) {
  if (node.isNull() || depth > 6) return false;

  if (node.is<float>()) {
    float f = node.as<float>();
    if (f >= -50.0f && f <= 200.0f) {
      outValue = (int)lroundf(f);
      return true;
    }
    return false;
  }

  if (node.is<JsonArray>()) {
    for (JsonVariant item : node.as<JsonArray>()) {
      if (extractReading(item, outValue, depth + 1)) return true;
    }
    return false;
  }

  if (node.is<JsonObject>()) {
    JsonObject obj = node.as<JsonObject>();

    if (!obj["value"].isNull() && obj["value"].is<float>()) {
      float f = obj["value"].as<float>();
      if (f >= -50.0f && f <= 200.0f) {
        outValue = (int)lroundf(f);
        return true;
      }
    }

    for (JsonPair kv : obj) {
      const char* key = kv.key().c_str();
      if (strcmp(key, "stations")       == 0 ||
          strcmp(key, "location")       == 0 ||
          strcmp(key, "label_location") == 0 ||
          strcmp(key, "valid_period")   == 0) {
        continue;
      }
      if (extractReading(kv.value(), outValue, depth + 1)) return true;
    }
  }

  return false;
}

// ============================================================
// FETCH ONE NUMERIC READING (temperature OR humidity)
// ============================================================
bool fetchReading(WiFiClientSecure &tls, const char* url,
                  const char* label, int &outValue) {
  HTTPClient http;
  if (!http.begin(tls, url)) {
    Serial.printf("  [%s] http.begin() failed\n", label);
    return false;
  }

  http.addHeader("x-api-key", DATA_GOV_SG_API_KEY);
  http.addHeader("accept", "application/json");
  http.setTimeout(10000);

  int code = http.GET();
  Serial.printf("  [%s] HTTP Code: %d\n", label, code);

  if (code != HTTP_CODE_OK) {
    http.end();
    return false;
  }

  String payload = http.getString();
  http.end();

  JsonDocument doc;
  DeserializationError err = deserializeJson(doc, payload);
  if (err) {
    Serial.printf("  [%s] JSON error: %s\n", label, err.c_str());
    return false;
  }

  JsonVariant readings = doc["data"]["readings"];
  if (readings.isNull()) readings = doc["data"]["items"][0]["readings"];

  bool ok = false;
  if (!readings.isNull()) {
    ok = extractReading(readings, outValue);
  }
  if (!ok) {
    ok = extractReading(doc["data"], outValue);
  }

  if (ok) {
    Serial.printf("  [%s] SUCCESS: %d\n", label, outValue);
  } else {
    Serial.printf("  [%s] No numeric reading found. Snippet:\n", label);
    Serial.println(payload.substring(0, 300));
  }

  return ok;
}

// ============================================================
// MAIN WEATHER FETCH
// ============================================================
void fetchWeatherData() {
  Serial.println("\n========== WEATHER FETCH START ==========");

  WiFiClientSecure tls;
  tls.setInsecure();
  tls.setTimeout(10000);

  // ---- 1. Forecast: Hougang ----
  {
    HTTPClient http;
    if (http.begin(tls, "https://api-open.data.gov.sg/v2/real-time/api/two-hr-forecast")) {
      http.addHeader("x-api-key", DATA_GOV_SG_API_KEY);
      http.setTimeout(10000);

      if (http.GET() == HTTP_CODE_OK) {
        JsonDocument doc;
        if (!deserializeJson(doc, http.getString())) {
          JsonArray items = doc["data"]["items"];
          if (items.size() > 0) {
            for (JsonObject f : items[0]["forecasts"].as<JsonArray>()) {
              if (f["area"].as<String>() == "Hougang") {
                weather.forecast = f["forecast"].as<String>();
                Serial.printf("  [Forecast] SUCCESS: %s\n", weather.forecast.c_str());
                break;
              }
            }
          }
        }
      }
      http.end();
    }
  }

  // ---- 2 & 3. Temperature and Humidity ----
  bool tempOK = fetchReading(tls,
      "https://api-open.data.gov.sg/v2/real-time/api/air-temperature",
      "Temperature", weather.tempC);

  bool humOK = fetchReading(tls,
      "https://api-open.data.gov.sg/v2/real-time/api/relative-humidity",
      "Humidity", weather.humidity);

  weather.isValid = tempOK && humOK;

  Serial.printf("========== WEATHER FETCH COMPLETE ==========\n");
  Serial.printf("isValid=%d | temp=%d | humidity=%d | forecast=%s\n\n",
      weather.isValid, weather.tempC, weather.humidity, weather.forecast.c_str());
}

int calculateETA(const char* isoTime) {
  if (isoTime == NULL || strlen(isoTime) == 0) return -1;
  
  time_t currentEpoch = time(NULL);
  if (currentEpoch < 1700000000) return -1;

  char timeStr[20];
  strncpy(timeStr, isoTime, 19);
  timeStr[19] = '\0';
  
  struct tm arrivalTime = {0};
  if (strptime(timeStr, "%Y-%m-%dT%H:%M:%S", &arrivalTime) != NULL) {
    time_t arrivalEpoch = mktime(&arrivalTime);
    long diffSeconds = arrivalEpoch - currentEpoch;
    int diffMinutes = diffSeconds / 60;
    
    if (diffMinutes < 0) return -1;
    if (diffMinutes < 1) return 0;
    return diffMinutes;
  }
  return -1;
}

// ==========================================
// 6. BATTERY MONITORING (Waveshare 3x Divider Fix)
// ==========================================
uint8_t getBatteryPercent() {
  analogSetAttenuation(ADC_11db);
  analogRead(BATT_ADC_PIN);
  delay(10);
  
  int rawMv = analogReadMilliVolts(BATT_ADC_PIN);
  int actualMv = rawMv * 3.0;
  
  Serial.printf("Battery Debug -> Raw: %dmV | Actual: %dmV\n", rawMv, actualMv);

  if (actualMv <= 2500) return 0;
  if (actualMv >= 4200) return 100;
  
  int percent = (actualMv - 2500) * 100 / (4200 - 2500);
  return constrain(percent, 0, 100);
}

// ==========================================
// 7. VECTOR WEATHER ICONS (32x32 px)
// ==========================================
WeatherIcon classifyWeather(const char* f) {
  if (strstr(f, "Thundery")) return WI_THUNDER;
  if (strstr(f, "Rain") || strstr(f, "Showers")) return WI_RAIN;
  if (strstr(f, "Mist") || strstr(f, "Fog")) return WI_FOG;
  if (strstr(f, "Partly Cloudy")) return WI_PARTLY;
  if (strstr(f, "Cloudy") || strstr(f, "Hazy")) return WI_CLOUD;
  if (strstr(f, "Night")) return WI_MOON;
  if (strstr(f, "Fair") || strstr(f, "Windy")) return WI_SUN;
  return WI_CLOUD;
}

static void iconCloudShape(int x, int y) {
  u8g2.drawDisc(x + 11, y + 17, 6);
  u8g2.drawDisc(x + 21, y + 14, 7);
  u8g2.drawDisc(x + 25, y + 18, 4);
  u8g2.drawBox (x +  9, y + 17, 19, 6);
}

static void iconSun(int x, int y) {
  int cx = x + 16, cy = y + 16;
  u8g2.drawDisc(cx, cy, 6);
  const int dx[8] = { 9, 6, 0, -6, -9, -6, 0, 6 };
  const int dy[8] = { 0, 6, 9,  6,  0, -6, -9, -6 };
  const int ex[8] = {14,10, 0,-10,-14,-10,  0,10 };
  const int ey[8] = { 0,10,14, 10,  0,-10,-14,-10 };
  for (int i = 0; i < 8; i++) {
    u8g2.drawLine(cx + dx[i], cy + dy[i], cx + ex[i], cy + ey[i]);
  }
}

static void iconMoon(int x, int y) {
  u8g2.drawDisc(x + 16, y + 16, 11);
  u8g2.setDrawColor(0);
  u8g2.drawDisc(x + 22, y + 12, 10);
  u8g2.setDrawColor(1);
}

static void iconPartly(int x, int y) {
  u8g2.drawDisc(x + 24, y + 8, 4);
  u8g2.drawLine(x + 24, y + 1, x + 24, y + 2);
  u8g2.drawLine(x + 30, y + 8, x + 31, y + 8);
  u8g2.drawLine(x + 29, y + 3, x + 30, y + 4);
  iconCloudShape(x - 2, y + 8);
}

static void iconRain(int x, int y, boolean storm) {
  iconCloudShape(x, y - 5);
  if (storm) {
    u8g2.drawLine(x + 18, y + 21, x + 13, y + 27);
    u8g2.drawLine(x + 13, y + 27, x + 19, y + 27);
    u8g2.drawLine(x + 19, y + 27, x + 14, y + 31);
  } else {
    for (int i = 0; i < 3; i++) {
      int rx = x + 10 + i * 7;
      u8g2.drawLine(rx, y + 24, rx - 2, y + 29);
    }
  }
}

static void iconFog(int x, int y) {
  iconCloudShape(x, y - 7);
  u8g2.drawLine(x + 7,  y + 24, x + 26, y + 24);
  u8g2.drawLine(x + 10, y + 27, x + 29, y + 27);
  u8g2.drawLine(x + 6,  y + 30, x + 24, y + 30);
}

void drawWeatherIcon(int x, int y, const char* forecast) {
  u8g2.setDrawColor(1);
  switch (classifyWeather(forecast)) {
    case WI_SUN:     iconSun(x, y);                break;
    case WI_MOON:    iconMoon(x, y);               break;
    case WI_PARTLY:  iconPartly(x, y);             break;
    case WI_RAIN:    iconRain(x, y, false);        break;
    case WI_THUNDER: iconRain(x, y, true);         break;
    case WI_FOG:     iconFog(x, y);                break;
    default:         iconCloudShape(x, y);         break;
  }
}

// ==========================================
// 8. UI RENDERING
// ==========================================
void drawBusPage() {
  u8g2.clearBuffer();
  drawHeader();
  drawColumn(10, 45, "Opp Blk 232", busDataStop1);
  drawColumn(210, 45, "Blk 232", busDataStop2);
  drawWeather();
  drawFooter();
  u8g2.sendBuffer();
}

void drawHeader() {
  struct tm timeinfo;
  if (getLocalTime(&timeinfo)) {
    char dateStr[25], timeStr[15];
    strftime(dateStr, sizeof(dateStr), "%a, %d %b %Y", &timeinfo);
    strftime(timeStr, sizeof(timeStr), "%H:%M", &timeinfo);

    for (int i = 0; dateStr[i] != '\0'; i++) {
      dateStr[i] = toupper(dateStr[i]);
    }

    u8g2.setFont(u8g2_font_7x13B_tf);
    u8g2.drawStr(10, 20, dateStr);

    u8g2.setFont(u8g2_font_logisoso20_tf);
    int timeWidth = u8g2.getStrWidth(timeStr);
    u8g2.drawStr((400 - timeWidth) / 2, 24, timeStr);

    uint8_t battPct = getBatteryPercent();
    char battStr[10];
    snprintf(battStr, sizeof(battStr), "%d%%", battPct);
    u8g2.setFont(u8g2_font_7x13_tf);
    int battWidth = u8g2.getStrWidth(battStr);
    u8g2.drawStr(390 - battWidth, 20, battStr);
    
    u8g2.drawFrame(395 - battWidth - 20, 8, 16, 10);
    u8g2.drawFrame(395 - battWidth - 22, 11, 2, 4);
    int fillWidth = map(battPct, 0, 100, 0, 14);
    u8g2.drawBox(395 - battWidth - 19, 9, fillWidth, 8);
  }
}

void drawWeather() {
  if (!weather.isValid) {
    u8g2.setFont(u8g2_font_7x13_tf);
    u8g2.drawStr(10, 255, "Weather data unavailable");
    return;
  }

  u8g2.drawHLine(0, 232, 400);
  u8g2.drawHLine(0, 268, 400);

  u8g2.setFont(u8g2_font_fub20_tf);
  char humStr[15];
  snprintf(humStr, sizeof(humStr), "%d%%", weather.humidity);
  u8g2.drawStr(10, 263, humStr);

  u8g2.setFont(u8g2_font_fub20_tf);
  char tempNumStr[10];
  snprintf(tempNumStr, sizeof(tempNumStr), "%d", weather.tempC);
  int tempNumWidth = u8g2.getStrWidth(tempNumStr);
  
  int tempX = (400 - tempNumWidth) / 2;
  u8g2.drawStr(tempX, 263, tempNumStr);
  
  int degreeX = tempX + tempNumWidth + 2;
  u8g2.drawGlyph(degreeX, 263, 0x00B0);
  u8g2.drawStr(degreeX + 12, 263, "C");

  // 32x32 icon box: x=358..389, y=234..265
  drawWeatherIcon(358, 234, weather.forecast.c_str());
}

void drawFooter() {
  struct tm timeinfo;
  if (getLocalTime(&timeinfo)) {
    char timeStr[10];
    strftime(timeStr, sizeof(timeStr), "%H:%M", &timeinfo);
    char footer[50];
    snprintf(footer, sizeof(footer), "Updated: %s | %lus", timeStr, currentUpdateInterval / 1000);
    u8g2.setFont(u8g2_font_5x8_tf);
    u8g2.drawStr(10, 290, footer);
  }
}

void drawColumn(int x, int y, const char* label, BusService* busArray) {
  u8g2.setDrawColor(1);
  u8g2.drawBox(x, y, 180, 22);
  u8g2.setDrawColor(0);
  u8g2.setFont(u8g2_font_9x15B_tf);
  u8g2.drawStr(x + 5, y + 16, label);
  u8g2.setDrawColor(1);

  int currentY = y + 45;
  int lineSpacing = 30;

  for (int i = 0; i < NUM_TARGET_BUSES; i++) {
    u8g2.setFont(u8g2_font_fub14_tf);
    u8g2.drawStr(x, currentY, busArray[i].serviceNo.c_str());

    u8g2.setFont(u8g2_font_fub17_tf);
    int etaX = x + 55;
    
    if (busArray[i].eta1 >= 0) {
      char etaStr[5];
      snprintf(etaStr, sizeof(etaStr), busArray[i].eta1 == 0 ? "Arr" : "%d", busArray[i].eta1);
      u8g2.drawStr(etaX, currentY, etaStr);
    }
    
    if (busArray[i].eta2 >= 0) {
      char etaStr[5];
      snprintf(etaStr, sizeof(etaStr), busArray[i].eta2 == 0 ? "Arr" : "%d", busArray[i].eta2);
      u8g2.drawStr(etaX + 40, currentY, etaStr);
    }
    
    if (busArray[i].eta3 >= 0) {
      char etaStr[5];
      snprintf(etaStr, sizeof(etaStr), busArray[i].eta3 == 0 ? "Arr" : "%d", busArray[i].eta3);
      u8g2.drawStr(etaX + 80, currentY, etaStr);
    }

    currentY += lineSpacing;
  }
}