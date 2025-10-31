/*
 * ESP32 集成系统
 * 功能：OLED显示 + 触摸控制 + Web服务器 + RESTful API
 * 作者：整合版本
 * 日期：2025-10-31
 */

// ==================== 库引入 ====================
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <WiFi.h>
#include <WebServer.h>
#include <DNSServer.h>
#include <NTPClient.h>
#include <WiFiUdp.h>
#include <time.h>

// ==================== WiFi配置 ====================
// 工作模式选择：
// 0 = 仅Station模式(连接WiFi)
// 1 = 仅AP模式(热点)
// 2 = AP+STA模式(同时连接WiFi并提供热点) 推荐！
#define WIFI_MODE 2

// Station模式配置（连接现有WiFi）
const char* sta_ssid = "vivox200";
const char* sta_password = "12345678";

// AP模式配置（创建热点）
const char* ap_ssid = "ESP32_Smart_Device";
const char* ap_password = "12345678";

// ==================== OLED配置 ====================
#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
#define OLED_RESET -1
#define OLED_SDA 13
#define OLED_SCL 12
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);

// ==================== 触摸配置 ====================
#define TOUCH_PIN 1
#define TOUCH_THRESHOLD 30000
#define TOUCH_DEBOUNCE 500

// ==================== 服务器配置 ====================
WebServer server(80);
DNSServer dnsServer;
const byte DNS_PORT = 53;

// NTP时间配置
WiFiUDP ntpUDP;
NTPClient timeClient(ntpUDP, "ntp.aliyun.com", 8 * 3600, 60000);

// ==================== 全局变量 ====================
int currentScreen = 0;          // 当前屏幕：0=主屏, 1=demo1, 2=demo2
unsigned long lastTouchTime = 0;
bool displayEnabled = true;      // 显示开关
int brightness = 255;            // 亮度（未使用，保留）
String customMessage = "";       // 自定义消息

// 设备状态
struct DeviceStatus {
  bool wifiConnected;
  String ipAddress;
  int currentScreen;
  bool displayEnabled;
  unsigned long uptime;
  int touchCount;
} deviceStatus;

// ==================== 函数声明 ====================
// JSON辅助函数
int parseJsonInt(String json, String key);
String parseJsonString(String json, String key);
bool parseJsonBool(String json, String key);

void setupWiFi();
void setupOLED();
void setupWebServer();
void setupNTP();
void checkTouch();
void updateDisplay();
void displayMainScreen();
void demo1();
void demo2();
void displayCustomMessage();

// Web处理函数
void handleRoot();
void handleAPI();
void handleGetStatus();
void handleSetScreen();
void handleSetDisplay();
void handleSetMessage();
void handleGetTime();
void handleNotFound();

// ==================== JSON辅助函数 ====================
// 简单的JSON解析函数(不依赖外部库)
int parseJsonInt(String json, String key) {
  int keyIndex = json.indexOf("\"" + key + "\"");
  if (keyIndex == -1) return -1;
  
  int colonIndex = json.indexOf(":", keyIndex);
  if (colonIndex == -1) return -1;
  
  int commaIndex = json.indexOf(",", colonIndex);
  int braceIndex = json.indexOf("}", colonIndex);
  int endIndex = (commaIndex != -1 && commaIndex < braceIndex) ? commaIndex : braceIndex;
  
  String value = json.substring(colonIndex + 1, endIndex);
  value.trim();
  return value.toInt();
}

String parseJsonString(String json, String key) {
  int keyIndex = json.indexOf("\"" + key + "\"");
  if (keyIndex == -1) return "";
  
  int colonIndex = json.indexOf(":", keyIndex);
  if (colonIndex == -1) return "";
  
  int firstQuote = json.indexOf("\"", colonIndex);
  if (firstQuote == -1) return "";
  
  int secondQuote = json.indexOf("\"", firstQuote + 1);
  if (secondQuote == -1) return "";
  
  return json.substring(firstQuote + 1, secondQuote);
}

bool parseJsonBool(String json, String key) {
  int keyIndex = json.indexOf("\"" + key + "\"");
  if (keyIndex == -1) return false;
  
  int colonIndex = json.indexOf(":", keyIndex);
  if (colonIndex == -1) return false;
  
  int trueIndex = json.indexOf("true", colonIndex);
  int falseIndex = json.indexOf("false", colonIndex);
  
  if (trueIndex != -1 && (falseIndex == -1 || trueIndex < falseIndex)) {
    return true;
  }
  return false;
}

// ==================== 初始化 ====================
void setup() {
  Serial.begin(115200);
  Serial.println("\n\n=== ESP32 Smart Device Starting ===");
  
  // 初始化各模块
  setupOLED();
  setupWiFi();
  setupNTP();
  setupWebServer();
  
  // 初始化设备状态
  deviceStatus.wifiConnected = (WiFi.status() == WL_CONNECTED);
  // AP+STA模式优先显示Station IP，如果没连上则显示AP IP
  if (WIFI_MODE == 2 && WiFi.status() == WL_CONNECTED) {
    deviceStatus.ipAddress = WiFi.localIP().toString();
  } else if (WIFI_MODE == 1 || WIFI_MODE == 2) {
    deviceStatus.ipAddress = WiFi.softAPIP().toString();
  } else {
    deviceStatus.ipAddress = WiFi.localIP().toString();
  }
  deviceStatus.currentScreen = 0;
  deviceStatus.displayEnabled = true;
  deviceStatus.uptime = millis();
  deviceStatus.touchCount = 0;
  
  Serial.println("=== System Ready ===");
  if (WIFI_MODE == 2) {
    Serial.println("AP IP: " + WiFi.softAPIP().toString());
    if (WiFi.status() == WL_CONNECTED) {
      Serial.println("Station IP: " + WiFi.localIP().toString());
    }
  } else {
    Serial.println("IP: " + deviceStatus.ipAddress);
  }
  Serial.println("API: http://" + deviceStatus.ipAddress + "/api");
}

// ==================== 主循环 ====================
void loop() {
  // 处理Web请求
  server.handleClient();
  
  // AP或AP+STA模式需要处理DNS请求
  if (WIFI_MODE == 1 || WIFI_MODE == 2) {
    dnsServer.processNextRequest();
  }
  
  // 更新时间（需要联网）
  if (WiFi.status() == WL_CONNECTED) {
    timeClient.update();
  }
  
  // 检查触摸
  checkTouch();
  
  // 更新显示
  if (displayEnabled) {
    updateDisplay();
  }
  
  delay(50);
}

// ==================== WiFi设置 ====================
void setupWiFi() {
  if (WIFI_MODE == 1) {
    // 仅AP模式（热点）
    Serial.println("Starting AP mode only...");
    WiFi.mode(WIFI_AP);
    WiFi.softAP(ap_ssid, ap_password);
    IPAddress apIP = WiFi.softAPIP();
    Serial.print("AP IP: ");
    Serial.println(apIP);
    
    // 启动DNS服务器
    dnsServer.start(DNS_PORT, "*", apIP);
    Serial.println("DNS server started");
    
  } else if (WIFI_MODE == 0) {
    // 仅Station模式（连接WiFi）
    Serial.print("Connecting to WiFi: ");
    Serial.println(sta_ssid);
    WiFi.mode(WIFI_STA);
    WiFi.begin(sta_ssid, sta_password);
    
    int attempts = 0;
    while (WiFi.status() != WL_CONNECTED && attempts < 20) {
      delay(500);
      Serial.print(".");
      attempts++;
    }
    
    if (WiFi.status() == WL_CONNECTED) {
      Serial.println("\nWiFi connected!");
      Serial.print("Station IP: ");
      Serial.println(WiFi.localIP());
    } else {
      Serial.println("\nWiFi connection failed!");
    }
    
  } else if (WIFI_MODE == 2) {
    // AP+STA模式（同时连接WiFi并提供热点）
    Serial.println("Starting AP+STA mode...");
    WiFi.mode(WIFI_AP_STA);
    
    // 先启动AP
    WiFi.softAP(ap_ssid, ap_password);
    IPAddress apIP = WiFi.softAPIP();
    Serial.print("AP IP: ");
    Serial.println(apIP);
    
    // 启动DNS服务器
    dnsServer.start(DNS_PORT, "*", apIP);
    Serial.println("DNS server started");
    
    // 然后连接WiFi
    Serial.print("Connecting to WiFi: ");
    Serial.println(sta_ssid);
    WiFi.begin(sta_ssid, sta_password);
    
    int attempts = 0;
    while (WiFi.status() != WL_CONNECTED && attempts < 20) {
      delay(500);
      Serial.print(".");
      attempts++;
    }
    
    if (WiFi.status() == WL_CONNECTED) {
      Serial.println("\nWiFi connected!");
      Serial.print("Station IP: ");
      Serial.println(WiFi.localIP());
      Serial.println("You can access via:");
      Serial.println("  - AP: http://" + apIP.toString());
      Serial.println("  - STA: http://" + WiFi.localIP().toString());
    } else {
      Serial.println("\nWiFi connection failed, but AP is still working!");
      Serial.println("Access via AP: http://" + apIP.toString());
    }
  }
}

// ==================== OLED设置 ====================
void setupOLED() {
  Wire.begin(OLED_SDA, OLED_SCL);
  
  if (!display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) {
    Serial.println("OLED initialization failed!");
    while (1);
  }
  
  display.clearDisplay();
  display.setTextColor(SSD1306_WHITE);
  display.setTextSize(1);
  display.setCursor(0, 0);
  display.println("System Starting...");
  display.display();
  
  Serial.println("OLED initialized");
}

// ==================== NTP设置 ====================
void setupNTP() {
  // Station模式或AP+STA模式下，如果连接了WiFi则启动NTP
  if (WIFI_MODE != 1 && WiFi.status() == WL_CONNECTED) {
    timeClient.begin();
    Serial.println("NTP client started");
  }
}

// ==================== Web服务器设置 ====================
void setupWebServer() {
  // 路由注册
  server.on("/", HTTP_GET, handleRoot);
  server.on("/api", HTTP_GET, handleAPI);
  
  // RESTful API端点
  server.on("/api/status", HTTP_GET, handleGetStatus);
  server.on("/api/screen", HTTP_POST, handleSetScreen);
  server.on("/api/display", HTTP_POST, handleSetDisplay);
  server.on("/api/message", HTTP_POST, handleSetMessage);
  server.on("/api/time", HTTP_GET, handleGetTime);
  
  server.onNotFound(handleNotFound);
  server.begin();
  
  Serial.println("Web server started");
}

// ==================== 触摸检测 ====================
void checkTouch() {
  int touchValue = touchRead(TOUCH_PIN);
  if (touchValue > TOUCH_THRESHOLD && millis() - lastTouchTime >= TOUCH_DEBOUNCE) {
    lastTouchTime = millis();
    currentScreen = (currentScreen + 1) % 4;  // 0-3循环
    deviceStatus.currentScreen = currentScreen;
    deviceStatus.touchCount++;
    Serial.print("Screen switched to: ");
    Serial.println(currentScreen);
  }
}

// ==================== 显示更新 ====================
void updateDisplay() {
  switch (currentScreen) {
    case 0:
      displayMainScreen();
      break;
    case 1:
      demo1();
      break;
    case 2:
      demo2();
      break;
    case 3:
      displayCustomMessage();
      break;
  }
}

// ==================== 显示屏幕 ====================
void displayMainScreen() {
  display.clearDisplay();
  
  if (WiFi.status() == WL_CONNECTED) {
    // 有网络连接，显示时间
    time_t epochTime = (time_t)timeClient.getEpochTime();
    struct tm timeInfo;
    gmtime_r(&epochTime, &timeInfo);
    
    display.setTextSize(3);
    display.setCursor(20, 5);
    if (timeInfo.tm_hour < 10) display.print("0");
    display.print(timeInfo.tm_hour);
    display.print(":");
    if (timeInfo.tm_min < 10) display.print("0");
    display.print(timeInfo.tm_min);
    
    display.setTextSize(1);
    display.setCursor(30, 40);
    display.print(timeInfo.tm_year + 1900);
    display.print("/");
    if ((timeInfo.tm_mon + 1) < 10) display.print("0");
    display.print(timeInfo.tm_mon + 1);
    display.print("/");
    if (timeInfo.tm_mday < 10) display.print("0");
    display.print(timeInfo.tm_mday);
    
    // AP+STA模式显示AP信息
    if (WIFI_MODE == 2) {
      display.setTextSize(1);
      display.setCursor(0, 55);
      display.print("AP:");
      display.print(WiFi.softAPIP());
    }
  } else {
    // 无网络连接，显示IP信息
    display.setTextSize(1);
    display.setCursor(0, 0);
    display.println("ESP32 Device");
    display.println("");
    
    if (WIFI_MODE == 2) {
      display.println("AP+STA Mode");
      display.print("AP IP:");
      display.println(WiFi.softAPIP());
      display.println("WiFi: Not Connected");
    } else if (WIFI_MODE == 1) {
      display.print("AP IP:");
      display.println(WiFi.softAPIP());
      display.println("");
      display.println("Connect WiFi:");
      display.println(ap_ssid);
    } else {
      display.print("IP:");
      display.println(WiFi.localIP());
    }
  }
  
  display.display();
}

void demo1() {
  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(0, 0);
  display.println("Demo 1 - Text");
  display.setCursor(0, 15);
  display.println("Touch to switch");
  
  display.setTextSize(2);
  display.setCursor(0, 35);
  display.print("COUNT:");
  display.print(deviceStatus.touchCount);
  
  display.display();
}

void demo2() {
  display.clearDisplay();
  display.drawRect(5, 5, 118, 54, SSD1306_WHITE);
  
  // 绘制正弦波
  for (int x = 5; x < 123; x++) {
    int y = 32 + sin((x - 5 + millis() / 50) * 0.1) * 15;
    display.drawPixel(x, y, SSD1306_WHITE);
  }
  
  display.display();
}

void displayCustomMessage() {
  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(0, 0);
  
  if (customMessage.length() > 0) {
    // 自动换行显示
    int lineHeight = 10;
    int maxChars = 21;  // 每行最多字符数
    int y = 0;
    
    for (int i = 0; i < customMessage.length(); i += maxChars) {
      display.setCursor(0, y);
      display.println(customMessage.substring(i, min(i + maxChars, (int)customMessage.length())));
      y += lineHeight;
      if (y >= SCREEN_HEIGHT) break;
    }
  } else {
    display.setCursor(0, 20);
    display.println("No custom message");
    display.setCursor(0, 35);
    display.println("Use API to set");
  }
  
  display.display();
}

// ==================== Web处理函数 ====================

// 主页面
void handleRoot() {
  String html = "<!DOCTYPE html><html><head><meta charset='UTF-8'>";
  html += "<meta name='viewport' content='width=device-width, initial-scale=1.0'>";
  html += "<title>ESP32 Smart Device</title><style>";
  html += "body{font-family:Arial,sans-serif;max-width:800px;margin:50px auto;padding:20px;background:#f5f5f5;}";
  html += ".container{background:white;padding:30px;border-radius:10px;box-shadow:0 2px 10px rgba(0,0,0,0.1);}";
  html += "h1{color:#333;}.status{background:#e8f5e9;padding:15px;border-radius:5px;margin:20px 0;}";
  html += ".controls{margin:20px 0;}button{background:#4CAF50;color:white;border:none;padding:10px 20px;";
  html += "margin:5px;border-radius:5px;cursor:pointer;font-size:14px;}button:hover{background:#45a049;}";
  html += ".api-link{display:inline-block;margin-top:20px;color:#1976d2;text-decoration:none;}";
  html += "input[type='text']{width:100%;padding:10px;margin:10px 0;border:1px solid #ddd;border-radius:5px;box-sizing:border-box;}";
  html += "</style></head><body><div class='container'>";
  html += "<h1>ESP32 Smart Device</h1><div class='status'><h3>Device Status</h3>";
  html += "<p>WiFi: <span id='wifi-status'>Loading...</span></p>";
  html += "<p>IP: <span id='ip'>Loading...</span></p>";
  html += "<p>Screen: <span id='screen'>Loading...</span></p>";
  html += "<p>Uptime: <span id='uptime'>Loading...</span></p>";
  html += "<p>Touch Count: <span id='touches'>Loading...</span></p></div>";
  html += "<div class='controls'><h3>Screen Control</h3>";
  html += "<button onclick='setScreen(0)'>Main</button>";
  html += "<button onclick='setScreen(1)'>Demo 1</button>";
  html += "<button onclick='setScreen(2)'>Demo 2</button>";
  html += "<button onclick='setScreen(3)'>Custom</button></div>";
  html += "<div class='controls'><h3>Display Control</h3>";
  html += "<button onclick='toggleDisplay(true)'>ON</button>";
  html += "<button onclick='toggleDisplay(false)'>OFF</button></div>";
  html += "<div class='controls'><h3>Custom Message</h3>";
  html += "<input type='text' id='message' placeholder='Enter message...'>";
  html += "<button onclick='sendMessage()'>Send Message</button></div>";
  html += "<a href='/api' class='api-link'>View API Docs</a></div>";
  html += "<script>";
  html += "function updateStatus(){fetch('/api/status').then(r=>r.json()).then(data=>{";
  html += "document.getElementById('wifi-status').textContent=data.wifiConnected?'Connected':'Disconnected';";
  html += "document.getElementById('ip').textContent=data.ipAddress;";
  html += "document.getElementById('screen').textContent=data.currentScreen;";
  html += "document.getElementById('uptime').textContent=Math.floor(data.uptime/1000)+'s';";
  html += "document.getElementById('touches').textContent=data.touchCount;});}";
  html += "function setScreen(screen){fetch('/api/screen',{method:'POST',headers:{'Content-Type':'application/json'},";
  html += "body:JSON.stringify({screen:screen})}).then(()=>setTimeout(updateStatus,100));}";
  html += "function toggleDisplay(enabled){fetch('/api/display',{method:'POST',headers:{'Content-Type':'application/json'},";
  html += "body:JSON.stringify({enabled:enabled})}).then(()=>setTimeout(updateStatus,100));}";
  html += "function sendMessage(){const msg=document.getElementById('message').value;";
  html += "fetch('/api/message',{method:'POST',headers:{'Content-Type':'application/json'},";
  html += "body:JSON.stringify({message:msg})}).then(()=>{setScreen(3);setTimeout(updateStatus,100);});}";
  html += "updateStatus();setInterval(updateStatus,2000);";
  html += "</script></body></html>";
  
  server.send(200, "text/html", html);
}

// API文档页面
void handleAPI() {
  String html = "<!DOCTYPE html><html><head><meta charset='UTF-8'><title>API Documentation</title>";
  html += "<style>body{font-family:'Courier New',monospace;max-width:900px;margin:30px auto;padding:20px;background:#1e1e1e;color:#d4d4d4;}";
  html += "h1,h2{color:#4EC9B0;}.endpoint{background:#252526;border-left:3px solid #4EC9B0;padding:15px;margin:15px 0;border-radius:5px;}";
  html += ".method{display:inline-block;padding:3px 8px;border-radius:3px;font-weight:bold;margin-right:10px;}";
  html += ".get{background:#61AFEF;color:white;}.post{background:#98C379;color:white;}";
  html += "code{background:#3E3E42;padding:2px 6px;border-radius:3px;color:#CE9178;}";
  html += "pre{background:#1e1e1e;border:1px solid #3E3E42;padding:15px;border-radius:5px;overflow-x:auto;}";
  html += "a{color:#4EC9B0;text-decoration:none;}a:hover{text-decoration:underline;}</style></head>";
  html += "<body><h1>ESP32 API Documentation</h1><p><a href='/'>Back to Home</a></p><h2>API Endpoints</h2>";
  
  // GET /api/status
  html += "<div class='endpoint'><span class='method get'>GET</span><code>/api/status</code>";
  html += "<p>Get device status</p><p><strong>Response:</strong></p>";
  html += "<pre>{\"wifiConnected\":true,\"ipAddress\":\"192.168.1.100\",\"currentScreen\":0,\"displayEnabled\":true,\"uptime\":123456,\"touchCount\":5}</pre></div>";
  
  // POST /api/screen
  html += "<div class='endpoint'><span class='method post'>POST</span><code>/api/screen</code>";
  html += "<p>Switch screen display</p><p><strong>Request:</strong></p>";
  html += "<pre>{\"screen\":0}  // 0=Main, 1=Demo1, 2=Demo2, 3=Custom</pre>";
  html += "<p><strong>Response:</strong></p><pre>{\"success\":true,\"screen\":0}</pre></div>";
  
  // POST /api/display
  html += "<div class='endpoint'><span class='method post'>POST</span><code>/api/display</code>";
  html += "<p>Control display on/off</p><p><strong>Request:</strong></p>";
  html += "<pre>{\"enabled\":true}  // true=ON, false=OFF</pre>";
  html += "<p><strong>Response:</strong></p><pre>{\"success\":true,\"enabled\":true}</pre></div>";
  
  // POST /api/message
  html += "<div class='endpoint'><span class='method post'>POST</span><code>/api/message</code>";
  html += "<p>Set custom message</p><p><strong>Request:</strong></p>";
  html += "<pre>{\"message\":\"Hello ESP32!\"}</pre>";
  html += "<p><strong>Response:</strong></p><pre>{\"success\":true,\"message\":\"Hello ESP32!\"}</pre></div>";
  
  // GET /api/time
  html += "<div class='endpoint'><span class='method get'>GET</span><code>/api/time</code>";
  html += "<p>Get current time (Station mode only)</p><p><strong>Response:</strong></p>";
  html += "<pre>{\"epoch\":1698765432,\"formatted\":\"2023-10-31 15:30:32\"}</pre></div>";
  
  // Usage examples
  html += "<h2>Usage Examples</h2><h3>JavaScript/Fetch</h3><pre>";
  html += "fetch('http://192.168.4.1/api/status')\\n  .then(r=>r.json())\\n  .then(data=>console.log(data));\\n\\n";
  html += "fetch('http://192.168.4.1/api/screen',{\\n  method:'POST',\\n  headers:{'Content-Type':'application/json'},\\n  body:JSON.stringify({screen:1})\\n});</pre>";
  
  html += "<h3>Python</h3><pre>import requests\\n\\nr=requests.get('http://192.168.4.1/api/status')\\nprint(r.json())\\n\\n";
  html += "r=requests.post('http://192.168.4.1/api/message',json={'message':'Hello!'})</pre>";
  
  html += "<h3>curl</h3><pre>curl http://192.168.4.1/api/status\\n\\n";
  html += "curl -X POST http://192.168.4.1/api/screen -H 'Content-Type:application/json' -d '{\"screen\":2}'</pre>";
  
  html += "</body></html>";
  
  server.send(200, "text/html", html);
}

// GET /api/status - 获取设备状态
void handleGetStatus() {
  String response = "{";
  response += "\"wifiConnected\":" + String((WiFi.status() == WL_CONNECTED) ? "true" : "false") + ",";
  response += "\"ipAddress\":\"" + deviceStatus.ipAddress + "\",";
  response += "\"currentScreen\":" + String(currentScreen) + ",";
  response += "\"displayEnabled\":" + String(displayEnabled ? "true" : "false") + ",";
  response += "\"uptime\":" + String(millis()) + ",";
  response += "\"touchCount\":" + String(deviceStatus.touchCount);
  response += "}";
  
  server.send(200, "application/json", response);
}

// POST /api/screen - 设置屏幕
void handleSetScreen() {
  if (server.method() != HTTP_POST) {
    server.send(405, "application/json", "{\"error\":\"Method not allowed\"}");
    return;
  }
  
  String body = server.arg("plain");
  int screen = parseJsonInt(body, "screen");
  
  if (screen >= 0 && screen <= 3) {
    currentScreen = screen;
    deviceStatus.currentScreen = currentScreen;
    
    String response = "{\"success\":true,\"screen\":" + String(currentScreen) + "}";
    server.send(200, "application/json", response);
  } else {
    server.send(400, "application/json", "{\"error\":\"Invalid screen value (0-3)\"}");
  }
}

// POST /api/display - 控制显示
void handleSetDisplay() {
  if (server.method() != HTTP_POST) {
    server.send(405, "application/json", "{\"error\":\"Method not allowed\"}");
    return;
  }
  
  String body = server.arg("plain");
  bool enabled = parseJsonBool(body, "enabled");
  
  displayEnabled = enabled;
  
  if (!displayEnabled) {
    display.clearDisplay();
    display.display();
  }
  
  String response = "{\"success\":true,\"enabled\":" + String(displayEnabled ? "true" : "false") + "}";
  server.send(200, "application/json", response);
}

// POST /api/message - 设置自定义消息
void handleSetMessage() {
  if (server.method() != HTTP_POST) {
    server.send(405, "application/json", "{\"error\":\"Method not allowed\"}");
    return;
  }
  
  String body = server.arg("plain");
  String message = parseJsonString(body, "message");
  
  if (message.length() > 0) {
    customMessage = message;
    
    String response = "{\"success\":true,\"message\":\"" + customMessage + "\"}";
    server.send(200, "application/json", response);
  } else {
    server.send(400, "application/json", "{\"error\":\"Missing or empty 'message' parameter\"}");
  }
}

// GET /api/time - 获取时间
void handleGetTime() {
  if (WiFi.status() == WL_CONNECTED) {
    time_t epochTime = (time_t)timeClient.getEpochTime();
    struct tm timeInfo;
    gmtime_r(&epochTime, &timeInfo);
    
    char timeStr[32];
    sprintf(timeStr, "%04d-%02d-%02d %02d:%02d:%02d",
            timeInfo.tm_year + 1900,
            timeInfo.tm_mon + 1,
            timeInfo.tm_mday,
            timeInfo.tm_hour,
            timeInfo.tm_min,
            timeInfo.tm_sec);
    
    String response = "{\"epoch\":" + String((long)epochTime) + ",\"formatted\":\"" + String(timeStr) + "\"}";
    server.send(200, "application/json", response);
  } else {
    server.send(503, "application/json", "{\"error\":\"Time service unavailable - no internet connection\"}");
  }
}

// 404处理
void handleNotFound() {
  String message = "{\"error\":\"Not Found\",\"uri\":\"" + server.uri() + "\"}";
  server.send(404, "application/json", message);
}
