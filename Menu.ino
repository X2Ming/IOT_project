#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <WiFi.h>
#include <NTPClient.h>
#include <WiFiUdp.h>
#include <time.h>
#include <Adafruit_NeoPixel.h>
#include "MAX30105.h"
#include "spo2_algorithm.h"
#include "heartRate.h"
#include <Preferences.h>
#include <WebServer.h>
#include <DNSServer.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>

// ---------------------- I2C ----------------------
TwoWire I2C_sensor(1);

// ---------------------- WiFi & Web Config ----------------------
Preferences preferences;
WebServer server(80);
DNSServer dnsServer;
const byte DNS_PORT = 53;

struct DeviceConfig {
  String wifi_ssid;
  String wifi_password;
  String server_url;
  String student_id;
  bool configured;
  int connection_attempts;
};

DeviceConfig deviceConfig;
const char* ap_ssid = "Config";
const char* ap_password = "12345678";

enum SystemState {
  STATE_CONFIG_PORTAL,
  STATE_CONNECTED,
  STATE_CONNECTION_FAILED
};

SystemState currentState = STATE_CONFIG_PORTAL;

// ---------------------- OLED ----------------------
#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
#define OLED_RESET -1 
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);

// ---------------------- Touch Keys ----------------------
#define TOUCH_LEFT 1      // GP1
#define TOUCH_SELECT 4    // GP4  
#define TOUCH_RIGHT 7     // GP7

#define TOUCH_THRESHOLD 30000
#define TOUCH_DEBOUNCE 500
#define INVALID_TOUCH_VALUE 4194303

// ---------------------- Screen State ----------------------
enum ScreenState {
  SCREEN_TIME,
  SCREEN_MENU,
  SCREEN_OVERALL,
  SCREEN_BPM,
  SCREEN_SPO2
};

enum MenuItem {
  MENU_OVERALL = 0,
  MENU_BPM = 1,
  MENU_SPO2 = 2,
  MENU_COUNT = 3
};

ScreenState currentScreen = SCREEN_TIME;
MenuItem selectedMenu = MENU_OVERALL;

// ---------------------- IR Data Upload (Feature 3.2) ----------------------
// 10s catch the data 50hz*10
#define MAX_SAMPLES 500 
#define SAMPLE_INTERVAL 20 // 20ms = 50Hz

struct IRSample {
  unsigned long timestamp;
  uint32_t ir_value;
};

IRSample irSamples[MAX_SAMPLES];
int sampleCount = 0;
unsigned long lastSampleTime = 0;
bool samplingActive = false;
unsigned long samplingStartTime = 0;

// Upload retry settings
#define UPLOAD_RETRY_COUNT 3
#define UPLOAD_RETRY_DELAY 2000

// ---------------------- Time ----------------------
WiFiUDP ntpUDP;
NTPClient timeClient(ntpUDP, "ntp.aliyun.com", 8 * 3600, 60000);

unsigned long lastTouchTimeLeft = 0;
unsigned long lastTouchTimeSelect = 0;
unsigned long lastTouchTimeRight = 0;
bool fingerDetected = false;

ScreenState screenBeforeTime = SCREEN_TIME;
unsigned long lastActivityTime = 0;
#define INACTIVITY_TIMEOUT 5000

unsigned long startupTime = 0;
#define STARTUP_DELAY 3000
bool sensorPresent = false;

// ---------------------- MAX30102 ----------------------
MAX30105 particleSensor;

#define MAX_BRIGHTNESS 255
#define BUFFER_SIZE 100

uint32_t irBuffer[BUFFER_SIZE];
uint32_t redBuffer[BUFFER_SIZE];
int32_t bufferHead = 0;
int32_t bufferTail = 0;

int32_t spo2 = 0;
int8_t validSPO2 = 0;
int32_t heartRate = 0;
int8_t validHeartRate = 0;

unsigned long lastPrintTime = 0;
const unsigned long PRINT_INTERVAL = 1000;
unsigned long lastBeat = 0;

// ---------------------- RGB LED ----------------------
#define RGB_PIN 48
#define RGB_LEDS 1
Adafruit_NeoPixel rgb(RGB_LEDS, RGB_PIN, NEO_GRB + NEO_KHZ800);
uint8_t breatheBrightness = 40;
int8_t breatheStep = 5;
unsigned long lastLedUpdate = 0;
const unsigned long LED_UPDATE_INTERVAL = 20;
uint8_t colorIndex = 0;

// ---------------------- Function Declarations ----------------------
void initSensors();
void checkTouch();
void handleLeftButton();
void handleSelectButton();
void handleRightButton();
void displayTimeScreen();
void displayMenuScreen();
void displayOverallHealthScreen();
void displayBPMScreen();
void displaySpO2Screen();
void readHeartRateAndSpO2();
void updateRgbLed();
uint32_t wheel(uint8_t pos);
bool isConfigValid();
void stopSamplingAndUpload(int bpm);

// =====================================================
//                 Data upload function
// =====================================================

bool uploadIRData(int bpm) {
  if (!isConfigValid()) {
    Serial.println("Cannot upload: Invalid configuration");
    return false;
  }
  
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("Cannot upload: WiFi not connected");
    return false;
  }
  
  if (sampleCount == 0) {
    Serial.println("No samples to upload");
    return false;
  }
  
  HTTPClient http;
  String url = deviceConfig.server_url + "/api/v1/ir/upload";
  
  Serial.println("Uploading data to: " + url);
  
  http.begin(url);
  http.addHeader("Content-Type", "application/json");
  
  JsonDocument doc; 
  doc["studentID"] = deviceConfig.student_id;
  doc["device"] = "minitor-esp32s3";
  doc["ts_ms"] = samplingStartTime;
  doc["bpm"] = bpm;
  
  JsonArray samples = doc["samples"].to<JsonArray>();
  for (int i = 0; i < sampleCount; i++) {
    JsonArray sample = samples.add<JsonArray>();
    sample.add(irSamples[i].timestamp - samplingStartTime); // time offset
    sample.add(irSamples[i].ir_value); // IR value
  }
  
  String jsonString;
  serializeJson(doc, jsonString);
  
  Serial.println("Uploading " + String(sampleCount) + " samples...");
  
  // Try upload with retries
  bool success = false;
  for (int attempt = 0; attempt < UPLOAD_RETRY_COUNT; attempt++) {
    int httpResponseCode = http.POST(jsonString);
    
    if (httpResponseCode > 0) {
      String response = http.getString();
      Serial.println("HTTP Response code: " + String(httpResponseCode));
      
      // Parse response
      JsonDocument respDoc;
      DeserializationError err = deserializeJson(respDoc, response);
      
      if (httpResponseCode == 200) {
        int code = respDoc["code"] | -1;
        if (code == 0) {
          Serial.println("Upload successful");

          // OLED Prompt
          display.clearDisplay();
          display.setTextSize(1);
          display.setTextColor(SSD1306_WHITE);
          display.setCursor(10, 20);
          display.println("Upload OK!");
          display.display();
          delay(1500);

          success = true;
          break; // Exit loop on success
        } else {
          Serial.println("Upload failed with code: " + String(code));
        }
      } 
    } else {
      Serial.println("HTTP Request failed: " + String(httpResponseCode));
    }
    
    if (attempt < UPLOAD_RETRY_COUNT - 1) {
      delay(UPLOAD_RETRY_DELAY);
    }
  }
  
  http.end();
  return success;
}

// Start sampling function
void startSampling() {
  if (samplingActive) return; // Already sampling
  
  samplingActive = true;
  sampleCount = 0;
  samplingStartTime = millis();
  lastSampleTime = samplingStartTime;
  
  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(10, 20);
  display.println("Sampling...");
  display.println("Wait 10s");
  display.display();
  
  Serial.println(">>> STARTED 10s IR DATA SAMPLING <<<");
}

// Stop sampling and upload data
void stopSamplingAndUpload(int bpm) {
  if (!samplingActive) return;
  
  samplingActive = false;
  Serial.println("Stopped sampling, collected " + String(sampleCount) + " samples");
  
  // Only upload if we have enough samples (e.g., > 90% of expected)
  if (sampleCount > (MAX_SAMPLES * 0.9)) {
    display.clearDisplay();
    display.setCursor(10, 20);
    display.println("Uploading...");
    display.display();
    
    uploadIRData(bpm);
  } else {
    Serial.println("Not enough samples to upload.");
    display.clearDisplay();
    display.setCursor(10, 20);
    display.println("Sample Fail");
    display.display();
    delay(1000);
  }
  
  sampleCount = 0;
}

// Add sample to buffer
void addIRSample(uint32_t irValue) {
  if (!samplingActive) return;
  
  unsigned long currentTime = millis();
  if (currentTime - lastSampleTime >= SAMPLE_INTERVAL) {
    if (sampleCount < MAX_SAMPLES) {
        irSamples[sampleCount].timestamp = currentTime;
        irSamples[sampleCount].ir_value = irValue;
        sampleCount++;
        lastSampleTime = currentTime;
    } else {
        // Buffer full (10 seconds reached)
        stopSamplingAndUpload(heartRate);
    }
  }
}

// =====================================================
//        Configuration Management Functions (Feature 3.1)
// =====================================================
void loadConfig() {
  preferences.begin("device-config", true);
  deviceConfig.wifi_ssid = preferences.getString("wifi_ssid", "");
  deviceConfig.wifi_password = preferences.getString("wifi_password", "");
  deviceConfig.server_url = preferences.getString("server_url", "");
  deviceConfig.student_id = preferences.getString("student_id", "");
  deviceConfig.configured = preferences.getBool("configured", false);
  deviceConfig.connection_attempts = preferences.getInt("conn_attempts", 0);
  preferences.end();
  
  Serial.println("=== Loading Configuration ===");
  Serial.println("SSID: " + deviceConfig.wifi_ssid);
  Serial.println("Server URL: " + deviceConfig.server_url);
  Serial.println("Student ID: " + deviceConfig.student_id);
}

void saveConfig() {
  preferences.begin("device-config", false);
  preferences.putString("wifi_ssid", deviceConfig.wifi_ssid);
  preferences.putString("wifi_password", deviceConfig.wifi_password);
  preferences.putString("server_url", deviceConfig.server_url);
  preferences.putString("student_id", deviceConfig.student_id);
  preferences.putBool("configured", true);
  preferences.putInt("conn_attempts", 0);
  preferences.end();
  
  deviceConfig.configured = true;
  Serial.println("Configuration saved successfully");
}

bool isConfigValid() {
  return deviceConfig.configured && 
         deviceConfig.wifi_ssid.length() > 0 && 
         deviceConfig.student_id.length() == 10 &&
         deviceConfig.server_url.length() > 0;
}

void clearConfig() {
  preferences.begin("device-config", false);
  preferences.clear();
  preferences.end();
  
  deviceConfig.configured = false;
  deviceConfig.wifi_ssid = "";
  deviceConfig.wifi_password = "";
  deviceConfig.server_url = "";
  deviceConfig.student_id = "";
  Serial.println("Configuration cleared");
}

// =====================================================
//              WiFi Connection Functions
// =====================================================
bool connectToWiFi() {
  if (!isConfigValid()) {
    Serial.println("Invalid configuration, cannot connect to WiFi");
    return false;
  }
  
  Serial.println("Attempting to connect to WiFi: " + deviceConfig.wifi_ssid);
  
  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(0, 20);
  display.println("Connecting to WiFi...");
  display.println(deviceConfig.wifi_ssid);
  display.display();
  
  WiFi.begin(deviceConfig.wifi_ssid.c_str(), deviceConfig.wifi_password.c_str());
  
  int attempts = 0;
  while (attempts < 20) {
    if (WiFi.status() == WL_CONNECTED) {
      Serial.println("WiFi connected successfully");
      Serial.println("IP Address: " + WiFi.localIP().toString());
      
      display.clearDisplay();
      display.setCursor(0, 20);
      display.println("WiFi Connected!");
      display.println("IP: " + WiFi.localIP().toString());
      display.display();
      delay(2000);
      
      return true;
    }
    delay(500);
    Serial.print(".");
    attempts++;
  }
  
  Serial.println("WiFi connection failed");
  display.clearDisplay();
  display.setCursor(0, 20);
  display.println("WiFi Connection Failed!");
  display.display();
  delay(2000);
  
  return false;
}

// =====================================================
//         Web Configuration Page Functions
// =====================================================
String generateConfigPage() {
  String html = R"(
<!DOCTYPE html><html><head>
  <meta charset='utf-8'><meta name='viewport' content='width=device-width, initial-scale=1.0'>
  <title>ESP32 Health Monitor Configuration</title>
  <style>
    body{font-family:Arial;margin:20px;background:#f0f0f0;}
    .container{background:white;padding:20px;border-radius:10px;box-shadow:0 2px 10px rgba(0,0,0,0.1);max-width:400px;margin:0 auto;}
    h2{text-align:center;color:#333;}
    label{display:block;margin:15px 0 5px;font-weight:bold;color:#555;}
    input{width:100%;padding:8px;border:1px solid #ddd;border-radius:5px;box-sizing:border-box;}
    button{background:#007bff;color:white;border:none;padding:10px;border-radius:5px;cursor:pointer;width:100%;margin:5px 0;}
    .clear-btn{background:#dc3545;}
    .status{text-align:center;padding:10px;margin:10px 0;border-radius:5px;}
    .connected{background:#d4edda;color:#155724;}
    .disconnected{background:#f8d7da;color:#721c24;}
    .note{font-size:12px;color:#666;margin-top:5px;}
  </style>
</head>
<body>
<div class='container'>
  <h2>ESP32 Health Monitor</h2>
  <div class='status disconnected'>Configuration Required</div>
  <form action='/save' method='POST'>
    <label>WiFi SSID:</label>
    <input name='ssid' value=')" + deviceConfig.wifi_ssid + R"(' placeholder='Enter WiFi name' required>
    
    <label>WiFi Password:</label>
    <input name='password' type='password' value=')" + deviceConfig.wifi_password + R"(' placeholder='Enter WiFi password'>
    
    <label>Server URL:</label>
    <input name='server_url' value=')" + deviceConfig.server_url + R"(' placeholder='http://apifj.com:16666' required>
    <div class='note'>Enter the complete server URL including protocol and port</div>
    
    <label>Student ID (10 characters):</label>
    <input name='student_id' value=')" + deviceConfig.student_id + R"(' placeholder='S123456789' maxlength='10' required>
    <div class='note'>Student ID must be exactly 10 characters long</div>
    
    <button type='submit'>Save Configuration</button>
  </form>
  <form action='/clear' method='POST'>
    <button class='clear-btn' type='submit'>Clear Configuration</button>
  </form>
</div>
</body>
</html>)";
  return html;
}

void handleRoot() { 
  server.send(200, "text/html; charset=UTF-8", generateConfigPage()); 
}

void handleSave() {
  deviceConfig.wifi_ssid = server.arg("ssid");
  deviceConfig.wifi_password = server.arg("password");
  deviceConfig.server_url = server.arg("server_url");
  deviceConfig.student_id = server.arg("student_id");
  
  // Validate required fields
  if (deviceConfig.wifi_ssid.length() == 0) {
    server.send(400, "text/plain", "WiFi name cannot be empty");
    return;
  }
  
  if (deviceConfig.server_url.length() == 0) {
    server.send(400, "text/plain", "Server URL cannot be empty");
    return;
  }
  
  if (deviceConfig.student_id.length() != 10) {
    server.send(400, "text/plain", "Student ID must be exactly 10 characters long");
    return;
  }
  
  saveConfig();
  server.send(200, "text/plain", "Configuration saved successfully. Device will restart and attempt to connect to WiFi.");
  delay(1000);
  ESP.restart();
}

void handleClear() {
  clearConfig();
  server.send(200, "text/plain", "Configuration cleared, device will restart");
  delay(1000);
  ESP.restart();
}

void handleNotFound() {
  server.sendHeader("Location", "http://" + WiFi.softAPIP().toString(), true);
  server.send(302, "text/plain", "");
}

void setupWebServer() {
  server.on("/", HTTP_GET, handleRoot);
  server.on("/save", HTTP_POST, handleSave);
  server.on("/clear", HTTP_POST, handleClear);
  server.onNotFound(handleNotFound);
  server.begin();
}

void startConfigPortal() {
  WiFi.disconnect(true);
  delay(100);
  WiFi.mode(WIFI_AP);
  WiFi.softAP(ap_ssid, ap_password);
  
  dnsServer.start(DNS_PORT, "*", WiFi.softAPIP());
  setupWebServer();
  
  Serial.println("Starting configuration AP: " + String(ap_ssid));
  Serial.println("Please connect to: " + WiFi.softAPIP().toString() + " for configuration");
  
  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(0, 10);
  display.println("Please configure WiFi");
  display.println("AP: " + String(ap_ssid));
  display.println("IP: " + WiFi.softAPIP().toString());
  display.println("Access via browser");
  display.display();
  
  currentState = STATE_CONFIG_PORTAL;
}

// =====================================================
//                      I2C Utils
// =====================================================
void scanI2C(TwoWire &bus, const char* tag) {
  Serial.print("Scanning I2C bus (");
  Serial.print(tag);
  Serial.println(")...");
  uint8_t found = 0;
  for (uint8_t address = 1; address < 127; address++) {
    bus.beginTransmission(address);
    uint8_t error = bus.endTransmission();
    if (error == 0) {
      Serial.print("  Found device address: 0x");
      Serial.println(address, HEX);
      found++;
    }
  }
  if (found == 0) {
    Serial.println("  No I2C devices found");
  }
}

bool isDevicePresent(TwoWire &bus, uint8_t address) {
  bus.beginTransmission(address);
  return (bus.endTransmission() == 0);
}

// =====================================================
//                        setup()
// =====================================================
void setup() {
  Serial.begin(115200);
  Serial.println("\n=== ESP32 Health Monitor Starting ===");
  
  Wire.begin(13, 12);
  I2C_sensor.begin(11, 10);
  Wire.setClock(400000);
  I2C_sensor.setClock(400000);
  
  if(!display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) {
    Serial.println("OLED initialization failed!");
    while(1);
  }

  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(0, 20);
  display.println("System starting...");
  display.display();

  loadConfig();
  
  if (isConfigValid() && connectToWiFi()) {
    currentState = STATE_CONNECTED;
    Serial.println("Entering normal operation mode");
    
    timeClient.begin();
    
    display.clearDisplay();
    display.setCursor(0, 20);
    display.println("Initializing sensors...");
    display.display();
    
    initSensors();
    
    currentScreen = SCREEN_MENU;
    selectedMenu = MENU_OVERALL;
    startupTime = millis();
    lastActivityTime = millis();
    
  } else {
    startConfigPortal();
  }
}

// =====================================================
//                    Sensor Init
// =====================================================
void initSensors() {
  scanI2C(Wire, "Wire");
  scanI2C(I2C_sensor, "I2C_sensor");

  Serial.println("Initializing MAX30102 sensor...");
  TwoWire* sensorBus = nullptr;
  if (isDevicePresent(Wire, 0x57)) {
    Serial.println("MAX30102 detected at 0x57 on Wire bus");
    sensorBus = &Wire;
  } else if (isDevicePresent(I2C_sensor, 0x57)) {
    Serial.println("MAX30102 detected at 0x57 on I2C_sensor bus");
    sensorBus = &I2C_sensor;
  }

  if (sensorBus == nullptr) {
    Serial.println("MAX30102 (0x57) not found on any bus; skipping sensor initialization");
    display.clearDisplay();
    display.setTextSize(1);
    display.setTextColor(SSD1306_WHITE);
    display.setCursor(0, 0);
    display.println("MAX30102 not detected");
    display.setCursor(0, 12);
    display.println("Skipping heart rate/SpO2");
    display.display();
    sensorPresent = false;
  } else {
    sensorPresent = particleSensor.begin(*sensorBus, I2C_SPEED_FAST);
    if (!sensorPresent) {
      Serial.println("MAX30102 initialization failed; skipping and continuing");
    } else {
      particleSensor.setup();
      particleSensor.setPulseAmplitudeRed(0x0A);
      particleSensor.setPulseAmplitudeIR(0x0A);
      particleSensor.setPulseAmplitudeGreen(0);
      Serial.println("MAX30102 initialization complete!");
    }
  }
  
  rgb.begin();
  rgb.setBrightness(breatheBrightness);
  rgb.setPixelColor(0, rgb.Color(255, 0, 0));
  rgb.show();
}

// =====================================================
//                        loop()
// =====================================================
void loop() {
  if (currentState == STATE_CONFIG_PORTAL) {
    dnsServer.processNextRequest();
    server.handleClient();
    return;
  }
  
  if (currentScreen != SCREEN_TIME && (millis() - lastActivityTime) > INACTIVITY_TIMEOUT) {
    // 如果正在采样，不要自动休眠
    if (!samplingActive) {
      screenBeforeTime = currentScreen;
      currentScreen = SCREEN_TIME;
    }
  }
  
  if(WiFi.status() != WL_CONNECTED) {
    Serial.println("WiFi disconnected, attempting to reconnect...");
    if (!connectToWiFi()) {
      Serial.println("Reconnection failed, starting configuration portal");
      startConfigPortal();
      return;
    }
  }
  
  timeClient.update();
  readHeartRateAndSpO2(); // 心率读取 & 采样逻辑入口
  updateRgbLed();
  checkTouch();

  switch(currentScreen) {
    case SCREEN_TIME:
      displayTimeScreen();
      break;
    case SCREEN_MENU:
      displayMenuScreen();
      break;
    case SCREEN_OVERALL:
      displayOverallHealthScreen();
      break;
    case SCREEN_BPM:
      displayBPMScreen();
      break;
    case SCREEN_SPO2:
      displaySpO2Screen();
      break;
  }

  delay(10); // Small delay to keep loop stable
}

// =====================================================
//                    Touch Handling
// =====================================================
void checkTouch() {
  if(millis() - startupTime < STARTUP_DELAY) {
    return;
  }
  
  int touchLeft = touchRead(TOUCH_LEFT);
  int touchSelect = touchRead(TOUCH_SELECT);
  int touchRight = touchRead(TOUCH_RIGHT);
  bool leftValid = (touchLeft > TOUCH_THRESHOLD && touchLeft < INVALID_TOUCH_VALUE);
  bool selectValid = (touchSelect > TOUCH_THRESHOLD && touchSelect < INVALID_TOUCH_VALUE);
  bool rightValid = (touchRight > TOUCH_THRESHOLD && touchRight < INVALID_TOUCH_VALUE);
  
  if (leftValid || selectValid || rightValid) {
    lastActivityTime = millis();
    if (currentScreen == SCREEN_TIME && screenBeforeTime != SCREEN_TIME) {
      unsigned long now = millis();
      if ((leftValid && now - lastTouchTimeLeft >= TOUCH_DEBOUNCE) ||
          (selectValid && now - lastTouchTimeSelect >= TOUCH_DEBOUNCE) ||
          (rightValid && now - lastTouchTimeRight >= TOUCH_DEBOUNCE)) {
        currentScreen = screenBeforeTime;
        lastTouchTimeLeft = now;
        lastTouchTimeSelect = now;
        lastTouchTimeRight = now;
        return;
      }
    }
  }
  
  if(leftValid && millis() - lastTouchTimeLeft >= TOUCH_DEBOUNCE) {
    lastTouchTimeLeft = millis();
    lastActivityTime = lastTouchTimeLeft;
    handleLeftButton();
    Serial.println(">>> Left button pressed");
  }
  
  if(selectValid && millis() - lastTouchTimeSelect >= TOUCH_DEBOUNCE) {
    lastTouchTimeSelect = millis();
    lastActivityTime = lastTouchTimeSelect;
    handleSelectButton();
    Serial.println(">>> Select button pressed");
  }
  
  if(rightValid && millis() - lastTouchTimeRight >= TOUCH_DEBOUNCE) {
    lastTouchTimeRight = millis();
    lastActivityTime = lastTouchTimeRight;
    handleRightButton();
    Serial.println(">>> Right button pressed");
  }
}

void handleLeftButton() {
  if(currentScreen == SCREEN_MENU) {
    selectedMenu = (MenuItem)((selectedMenu - 1 + MENU_COUNT) % MENU_COUNT);
  }
}

void handleSelectButton() {
  switch(currentScreen) {
    case SCREEN_MENU:
      switch(selectedMenu) {
        case MENU_OVERALL:
          currentScreen = SCREEN_OVERALL;
          break;
        case MENU_BPM:
          currentScreen = SCREEN_BPM;
          break;
        case MENU_SPO2:
          currentScreen = SCREEN_SPO2;
          break;
      }
      break;
    case SCREEN_OVERALL:
    case SCREEN_BPM:
    case SCREEN_SPO2:
      // 如果正在采样，不允许退出，或者强制停止
      if (samplingActive) {
         samplingActive = false; 
         Serial.println("Sampling cancelled by user");
      }
      currentScreen = SCREEN_MENU;
      break;
    case SCREEN_TIME:
      break;
  }
}

void handleRightButton() {
  if(currentScreen == SCREEN_MENU) {
    selectedMenu = (MenuItem)((selectedMenu + 1) % MENU_COUNT);
  }
}

// =====================================================
//                    Display Functions
// =====================================================
void displayTimeScreen() {
  display.clearDisplay();
  time_t epochTime = (time_t)timeClient.getEpochTime();
  struct tm timeInfo;
  gmtime_r(&epochTime, &timeInfo);

  display.setTextSize(3);
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(20, 5);
  if(timeInfo.tm_hour < 10) display.print("0");
  display.print(timeInfo.tm_hour);
  display.print(":");
  if(timeInfo.tm_min < 10) display.print("0");
  display.print(timeInfo.tm_min);

  display.setTextSize(1);
  display.setCursor(25, 40);
  display.print(timeInfo.tm_year + 1900);
  display.print("/");
  if((timeInfo.tm_mon + 1) < 10) display.print("0");
  display.print(timeInfo.tm_mon + 1);
  display.print("/");
  if(timeInfo.tm_mday < 10) display.print("0");
  display.print(timeInfo.tm_mday);

  display.setCursor(5, 55);
  display.print("Press key to return");
  display.display();
}

void displayMenuScreen() {
  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(35, 2);
  display.println("MAIN MENU");
  display.drawLine(0, 12, 128, 12, SSD1306_WHITE);
  
  const char* menuItems[3] = {"Overall Health", "Heart Rate", "SpO2 Monitor"};
  for (int i = 0; i < 3; i++) {
    int yPos = 20 + i * 13;
    if (i == selectedMenu) {
      display.fillRect(5, yPos - 2, 118, 11, SSD1306_WHITE);
      display.setTextColor(SSD1306_BLACK);
      display.setCursor(10, yPos);
      display.print("> ");
      display.print(menuItems[i]);
      display.setTextColor(SSD1306_WHITE);
    } else {
      display.setCursor(12, yPos);
      display.print(menuItems[i]);
    }
  }
  
  display.setCursor(8, 56);
  display.print("L/R:Select SEL:OK");
  display.display();
}

void displayOverallHealthScreen() {
  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(15, 2);
  display.println("Overall Health");
  display.drawLine(0, 12, 128, 12, SSD1306_WHITE);
  display.setTextSize(1);
  display.setCursor(2, 20);
  display.print("BPM:");
  display.setTextSize(2);
  display.setCursor(60, 16);
  if (fingerDetected && validHeartRate && heartRate > 0) {
    display.print(heartRate);
  } else {
    display.print("--");
  }

  display.setTextSize(1);
  display.setCursor(2, 44);
  display.print("SpO2:");
  display.setTextSize(2);
  display.setCursor(60, 40);
  if (fingerDetected && validSPO2 && spo2 > 0 && spo2 <= 100) {
    display.print(spo2);
    display.print("%");
  } else {
    display.print("--");
  }

  display.setTextSize(1);
  display.setCursor(15, 56);
  display.print("SEL: Back to Menu");
  display.display();
}

void displayBPMScreen() {
  display.clearDisplay();
  
  // Countdown display during sampling
  if (samplingActive) {
    display.setTextSize(1);
    display.setTextColor(SSD1306_WHITE);
    
    // top title
    display.setCursor(35, 2);
    display.println("Sampling...");
    display.drawLine(0, 12, 128, 12, SSD1306_WHITE);
    
    // Calculate the remaining time (seconds)
    // 50Hz sampling rate, so divide by 50
    int remainingTime = (MAX_SAMPLES - sampleCount) / 50;
    
    // Display countdown
    display.setTextSize(2);
    display.setCursor(10, 25);
    display.print("Time: ");
    display.print(remainingTime);
    display.println("s");
    
    // Display the progress of sampling count
    display.setTextSize(1);
    display.setCursor(10, 50);
    display.print("Samples: ");
    display.print(sampleCount);
    display.print("/");
    display.print(MAX_SAMPLES);
    
    display.display();
    return;
  }
  
  // 下面是未采样时的正常显示（保持不变）
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(35, 2);
  display.println("Heart Rate");
  display.drawLine(0, 12, 128, 12, SSD1306_WHITE);
  display.setTextSize(2);
  display.setCursor(25, 25);
  if (fingerDetected && validHeartRate && heartRate > 0) {
    display.print(heartRate);
  } else {
    display.print("--");
  }
  display.setTextSize(1);
  display.setCursor(95, 35);
  display.print("BPM");
  display.setCursor(15, 56);
  display.print("SEL: Back to Menu");
  display.display();
}

void displaySpO2Screen() {
  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(32, 2);
  display.println("SpO2 Monitor");
  display.drawLine(0, 12, 128, 12, SSD1306_WHITE);
  display.setTextSize(2);
  display.setCursor(25, 25);
  if (fingerDetected && validSPO2 && spo2 > 0 && spo2 <= 100) {
    display.print(spo2);
  } else {
    display.print("--");
  }
  display.setTextSize(1);
  display.setCursor(90, 35);
  display.print("%");
  display.setCursor(15, 56);
  display.print("SEL: Back to Menu");
  display.display();
}

// =====================================================
//             Heart Rate & SpO2 Calculation
// =====================================================
void readHeartRateAndSpO2() {
  if (!sensorPresent) return;
  
  long irValue = particleSensor.getIR();
  long redValue = particleSensor.getRed();
  
  fingerDetected = (irValue >= 10000);
  if (fingerDetected) {
    lastActivityTime = millis();

    // 3.2  If sampling is in progress (Active), data must be collected no matter what
    if (samplingActive) {
       addIRSample(irValue);
    }

    if (checkForBeat(irValue)) {
      unsigned long now = millis();
      unsigned long delta = now - lastBeat;
      lastBeat = now;
      
      if (delta > 300 && delta < 3000) { // 20~200 BPM range
        int currentBPM = 60000 / delta;
        Serial.print("Heart Rate: ");
        Serial.print(currentBPM);
        Serial.println(" BPM");
        heartRate = currentBPM;
        validHeartRate = 1;
        
        // 3.2 Only start 10-second sampling when "BPM Only Screen" is in operation and no sampling is conducted
        if (currentScreen == SCREEN_BPM && !samplingActive) {
            Serial.println("Valid BPM detected in BPM Screen -> Triggering 10s Sampling");
            startSampling();
        }
      }
    }
  } else {
    // move finger
    if (samplingActive) {
      samplingActive = false;
      sampleCount = 0;
      Serial.println("Finger removed - Sampling Aborted");
    }

    heartRate = 0;
    validHeartRate = 0;
    spo2 = 0;
    validSPO2 = 0;
  }
  
  // Circular Buffer Logic for SpO2
  irBuffer[bufferHead] = irValue;
  redBuffer[bufferHead] = redValue;
  bufferHead = (bufferHead + 1) % BUFFER_SIZE;
  
  if ((bufferHead + 1) % BUFFER_SIZE == bufferTail) {
    //  Linearize buffer data to the algorithm
    uint32_t irLinear[BUFFER_SIZE];
    uint32_t redLinear[BUFFER_SIZE];
    
    for (int i = 0; i < BUFFER_SIZE; i++) {
        int idx = (bufferTail + i) % BUFFER_SIZE;
        irLinear[i] = irBuffer[idx];
        redLinear[i] = redBuffer[idx];
    }

    maxim_heart_rate_and_oxygen_saturation(
      irLinear,
      BUFFER_SIZE,
      redLinear,
      &spo2,
      &validSPO2,
      &heartRate,
      &validHeartRate
    );
    
    bufferTail = (bufferTail + 1) % BUFFER_SIZE;
  }
}

// =====================================================
//                     RGB LED Effect
// =====================================================
void updateRgbLed() {
  unsigned long now = millis();
  if (now - lastLedUpdate < LED_UPDATE_INTERVAL) return;
  lastLedUpdate = now;

  if (!fingerDetected) {
    rgb.setBrightness(255);
    rgb.setPixelColor(0, rgb.Color(255, 0, 0));
    rgb.show();
    return;
  }

  breatheBrightness = constrain(breatheBrightness + breatheStep, 30, 255);
  if (breatheBrightness == 255 || breatheBrightness == 30) {
    breatheStep = -breatheStep;
  }
  rgb.setBrightness(breatheBrightness);
  colorIndex = (colorIndex + 1) % 256;
  rgb.setPixelColor(0, wheel(colorIndex));
  rgb.show();
}

uint32_t wheel(uint8_t pos) {
  if(pos < 85) {
    return rgb.Color(pos * 3, 255 - pos * 3, 0);
  } else if(pos < 170) {
    pos -= 85;
    return rgb.Color(255 - pos * 3, 0, pos * 3);
  } else {
    pos -= 170;
    return rgb.Color(0, pos * 3, 255 - pos * 3);
  }
}
