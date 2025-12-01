# ESP32 IoT Integrated System

This project is a comprehensive ESP32-based IoT system featuring an OLED display, touch controls, a web server with a RESTful API, and NTP time synchronization. It is designed to demonstrate how to integrate hardware peripherals with web-based remote control capabilities.

## 🌟 Features

*   **OLED Display**: Supports SSD1306 (128x64) screens to show status, time, demos, and custom messages.
*   **Touch Control**: Uses ESP32's capacitive touch pins to switch between display screens physically.
*   **Web Interface**: Built-in responsive web dashboard to view device status and control the screen remotely.
*   **RESTful API**: JSON-based API for integrating with other systems or custom apps (Python, simple websites, etc.).
*   **WiFi Modes**:
    *   **Station Mode**: Connects to your home router.
    *   **AP Mode**: Creates its own WiFi hotspot (`ESP32_Smart_Device`).
    *   **AP+STA Mode**: (Default) Connects to home WiFi while simultaneously providing a backup hotspot.
*   **NTP Time Sync**: Automatically fetches accurate time from the internet (via Aliyun NTP) when connected.

## 🛠 Hardware Requirements

*   **ESP32 Development Board**
*   **0.96" SSD1306 OLED Display** (I2C)
*   Jumper wires
*   (Optional) Battery for portable use

### Pin Configuration

| Component | ESP32 Pin | Notes |
| :--- | :--- | :--- |
| **OLED SDA** | GPIO 13 | I2C Data |
| **OLED SCL** | GPIO 12 | I2C Clock |
| **Touch Input** | GPIO 1 | Touch Pin (T1) |

*> **Note**: Ensure your specific ESP32 board supports using GPIO 1 for touch, as pinouts can vary between manufacturers.*

## 📦 Software Dependencies

To compile this project using the Arduino IDE, you need to install the following libraries via the Library Manager:

1.  `Adafruit GFX Library`
2.  `Adafruit SSD1306`
3.  `NTPClient`

*Core libraries included (WiFi, WebServer, DNSServer, Wire) are part of the ESP32 board package.*

## 🚀 Installation & Setup

1.  **Clone the Repository**:
    ```bash
    git clone https://github.com/X2Ming/IOT_project.git
    ```

2.  **Configure WiFi**:
    Open `ESP32_webJson.ino` and locate the WiFi configuration section (Lines 26-32). Update the credentials:
    ```cpp
    // Station Mode (Your Router)
    const char* sta_ssid = "your_wifi_name";
    const char* sta_password = "your_wifi_password";

    // AP Mode (Hotspot Name)
    const char* ap_ssid = "ESP32_Smart_Device";
    const char* ap_password = "12345678";
    ```

3.  **Upload**:
    *   Select your ESP32 board in Arduino IDE.
    *   Connect the ESP32 via USB.
    *   Click Upload.

## 📖 Usage

### Physical Control
*   **Touch GPIO 1**: Cycle through the different screens:
    1.  **Main Screen**: Time/Date and IP Address.
    2.  **Demo 1**: Text demo showing touch count.
    3.  **Demo 2**: Animated sine wave.
    4.  **Custom Message**: Displays message set via Web API.

### Web Control
Once the device is running, open the Serial Monitor (115200 baud) to find the IP address.
*   **Access Dashboard**: Open `http://<ESP32-IP>/` in your browser.
*   **Access API Docs**: Open `http://<ESP32-IP>/api` for detailed API usage.

### WiFi Modes
The device defaults to `WIFI_MODE 2` (AP+STA):
*   It attempts to connect to your router. If successful, access via the **Station IP**.
*   It *always* broadcasts a hotspot named **ESP32_Smart_Device**. You can connect to this network (Password: `12345678`) and access the device at `http://192.168.4.1`.

## 📡 API Reference

The device exposes a JSON API.

| Method | Endpoint | Description | Payload Example |
| :--- | :--- | :--- | :--- |
| `GET` | `/api/status` | Get device info (IP, Uptime, etc.) | - |
| `POST` | `/api/screen` | Switch active screen | `{"screen": 1}` |
| `POST` | `/api/display` | Turn OLED On/Off | `{"enabled": false}` |
| `POST` | `/api/message` | Set custom text | `{"message": "Hello World"}` |
| `GET` | `/api/time` | Get current epoch time | - |

## 📄 License

This project is open source. Feel free to modify and distribute.
