#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <WiFi.h>
#include <NTPClient.h>
#include <WiFiUdp.h>
#include <time.h>

// WiFi配置
const char* ssid = "614网瘾少年电疗中心";
const char* password = "114514114514";

// OLED配置
#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
#define OLED_RESET -1 
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);

// 三个触摸按键
#define TOUCH_LEFT 1      // GP1
#define TOUCH_SELECT 4    // GP4  
#define TOUCH_RIGHT 7     // GP7

// 调整触摸参数
#define TOUCH_THRESHOLD 30000  // 触摸后数值增大，所以用大于阈值
#define TOUCH_DEBOUNCE 500
#define INVALID_TOUCH_VALUE 4194303  // 引脚未连接时的异常值

// NTP时间配置
WiFiUDP ntpUDP;
NTPClient timeClient(ntpUDP, "ntp.aliyun.com", 8 * 3600, 60000);

// 界面状态枚举
enum ScreenState {
  SCREEN_TIME,
  SCREEN_MENU,
  SCREEN_OVERALL,
  SCREEN_BPM,
  SCREEN_SPO2
};

enum MenuItem {
  MENU_BACK_TO_TIME = 0,
  MENU_OVERALL = 1,
  MENU_BPM = 2,
  MENU_SPO2 = 3,
  MENU_COUNT = 4
};

// 全局变量
ScreenState currentScreen = SCREEN_TIME;
MenuItem selectedMenu = MENU_OVERALL;
// 为每个按键设置独立的防抖时间
unsigned long lastTouchTimeLeft = 0;
unsigned long lastTouchTimeSelect = 0;
unsigned long lastTouchTimeRight = 0;
bool fingerDetected = false;

// 添加启动保护变量
unsigned long startupTime = 0;
#define STARTUP_DELAY 3000  // 启动后3秒内忽略触摸


void setup() {
  Serial.begin(115200);
  Wire.begin(13, 12);

  if(!display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) {
    Serial.println("OLED初始化失败!");
    while(1);
  }

  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(0, 20);
  display.println("  Connecting WiFi...");
  display.display();

  connectToWiFi();
  
  timeClient.begin();
  display.clearDisplay();
  display.setCursor(0, 20);
  display.println("  WiFi Connected!");
  display.setCursor(0, 35);
  display.println("  Syncing time...");
  display.display();
  delay(2000);

  Serial.println("系统初始化完成");
  currentScreen = SCREEN_TIME;  // 确保开机在时间界面
  startupTime = millis();  // 记录启动时间
}


void loop() {
  if(WiFi.status() != WL_CONNECTED) {
    reconnectWiFi();
  }
  
  timeClient.update();
  checkTouch();  // 检查三个按键

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

  delay(100);
}


void connectToWiFi() {
  Serial.print("连接WiFi: ");
  Serial.println(ssid);
  WiFi.begin(ssid, password);

  while(WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }

  Serial.println("\nWiFi已连接");
  Serial.print("IP地址: ");
  Serial.println(WiFi.localIP());
}


void reconnectWiFi() {
  if(WiFi.status() != WL_CONNECTED) {
    Serial.println("WiFi断开，正在重连...");
    WiFi.reconnect();
  }
}


// 修正后的触摸检测函数
void checkTouch() {
  // 启动保护：启动后一段时间内忽略触摸
  if(millis() - startupTime < STARTUP_DELAY) {
    return;
  }
  
  // 读取三个按键的值
  int touchLeft = touchRead(TOUCH_LEFT);
  int touchSelect = touchRead(TOUCH_SELECT);
  int touchRight = touchRead(TOUCH_RIGHT);
  
  // 打印所有值用于调试
  Serial.print("L=");
  Serial.print(touchLeft);
  Serial.print(" S=");
  Serial.print(touchSelect);
  Serial.print(" R=");
  Serial.print(touchRight);
  Serial.print(" | 阈值=");
  Serial.println(TOUCH_THRESHOLD);

  // 触摸后数值增大，所以使用大于阈值判断，同时排除异常值
  // 左键
  if(touchLeft > TOUCH_THRESHOLD && touchLeft < INVALID_TOUCH_VALUE && millis() - lastTouchTimeLeft >= TOUCH_DEBOUNCE) {
    lastTouchTimeLeft = millis();
    handleLeftButton();
    Serial.println(">>> 左键触发");
  }
  
  // 选择键
  if(touchSelect > TOUCH_THRESHOLD && touchSelect < INVALID_TOUCH_VALUE && millis() - lastTouchTimeSelect >= TOUCH_DEBOUNCE) {
    lastTouchTimeSelect = millis();
    handleSelectButton();
    Serial.println(">>> 选择键触发");
  }
  
  // 右键
  if(touchRight > TOUCH_THRESHOLD && touchRight < INVALID_TOUCH_VALUE && millis() - lastTouchTimeRight >= TOUCH_DEBOUNCE) {
    lastTouchTimeRight = millis();
    handleRightButton();
    Serial.println(">>> 右键触发");
  }
}


void handleLeftButton() {
  if(currentScreen == SCREEN_MENU) {
    selectedMenu = (MenuItem)((selectedMenu - 1 + MENU_COUNT) % MENU_COUNT);
  }
}


void handleSelectButton() {
  switch(currentScreen) {
    case SCREEN_TIME:
      // 在时间界面按Select键进入菜单
      currentScreen = SCREEN_MENU;
      selectedMenu = MENU_BACK_TO_TIME; // 默认选择第一个菜单项
      break;
      
    case SCREEN_MENU:
      // 在菜单界面按Select键进入选中的功能或返回
      switch(selectedMenu) {
        case MENU_BACK_TO_TIME:
          currentScreen = SCREEN_TIME; // 返回时间界面
          break;
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
      // 在功能界面按Select键返回菜单
      currentScreen = SCREEN_MENU;
      break;
  }
}


void handleRightButton() {
  if(currentScreen == SCREEN_MENU) {
    selectedMenu = (MenuItem)((selectedMenu + 1) % MENU_COUNT);
  }
}


// ========== 界面显示函数 ==========

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

  // 修改提示文字，明确指示如何进入菜单
  display.setCursor(5, 55);
  display.print("SEL: Menu");
  
  display.display();
}


void displayMenuScreen() {
  display.clearDisplay();
  
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(35, 2);
  display.println("MAIN MENU");
  display.drawLine(0, 12, 128, 12, SSD1306_WHITE);
  
  // 定义菜单项
  const char* menuItems[] = {
    "Back to Time",    // 第一个
    "Overall Health",  // 第二个
    "BPM Only",        // 第三个
    "SpO2 Only"        // 第四个
  };
  
  // 计算当前页和总页数
  int currentPage = selectedMenu < 3 ? 0 : 1; // 前3项在第0页，第4项在第1页
  int totalPages = 2;
  
  // 显示当前页的菜单项
  if (currentPage == 0) {
    // 第一页：显示前三个菜单项
    for(int i = 0; i < 3; i++) {
      int yPos = 20 + i * 13;
      
      if(i == selectedMenu) {
        display.fillRect(5, yPos - 2, 118, 11, SSD1306_WHITE);
        display.setTextColor(SSD1306_BLACK);
        display.setCursor(10, yPos);
        display.print("> ");
        display.print(menuItems[i]);
      } else {
        display.setTextColor(SSD1306_WHITE);
        display.setCursor(12, yPos);
        display.print(menuItems[i]);
      }
    }
  } else {
    // 第二页：只显示第四个菜单项
    int yPos = 20;
    
    if(3 == selectedMenu) {
      display.fillRect(5, yPos - 2, 118, 11, SSD1306_WHITE);
      display.setTextColor(SSD1306_BLACK);
      display.setCursor(10, yPos);
      display.print("> ");
      display.print(menuItems[3]);
    } else {
      display.setTextColor(SSD1306_WHITE);
      display.setCursor(12, yPos);
      display.print(menuItems[3]);
    }
  }
  
  // 显示页面指示器
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(50, 45);
  display.print(currentPage + 1);
  display.print("/");
  display.print(totalPages);
  
  display.setCursor(8, 56);
  display.print("L/R:Page SEL:OK");
  
  display.display();
}


void displayOverallHealthScreen() {
  display.clearDisplay();
  
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(15, 2);
  display.println("Overall Health");
  display.drawLine(0, 12, 128, 12, SSD1306_WHITE);
  
  display.setCursor(5, 18);
  display.print("BPM:  ");
  display.print(fingerDetected ? "75" : "--");
  
  display.setCursor(5, 30);
  display.print("SpO2: ");
  display.print(fingerDetected ? "98%" : "--");
  
  display.setCursor(5, 42);
  display.print("IR:   ");
  display.print(fingerDetected ? "50000" : "--");
  
  display.setCursor(15, 56);
  display.print("SEL: Back to Menu");
  
  display.display();
}


void displayBPMScreen() {
  display.clearDisplay();
  
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(35, 2);
  display.println("BPM Only");
  display.drawLine(0, 12, 128, 12, SSD1306_WHITE);
  
  display.setTextSize(3);
  display.setCursor(25, 25);
  display.print(fingerDetected ? "75" : "--");
  
  display.setTextSize(1);
  display.setCursor(95, 35);
  display.print("bpm");
  
  display.setCursor(15, 56);
  display.print("SEL: Back to Menu");
  
  display.display();
}


void displaySpO2Screen() {
  display.clearDisplay();
  
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(32, 2);
  display.println("SpO2 Only");
  display.drawLine(0, 12, 128, 12, SSD1306_WHITE);
  
  display.setTextSize(3);
  display.setCursor(25, 25);
  display.print(fingerDetected ? "98" : "--");
  
  display.setTextSize(1);
  display.setCursor(90, 35);
  display.print("%");
  
  display.setCursor(15, 56);
  display.print("SEL: Back to Menu");
  
  display.display();
}