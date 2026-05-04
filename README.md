# Smart Class Monitoring Robot (ROBO ERIC PRO)

An ESP32-CAM based, web-controlled robot designed for remote monitoring with interactive audio feedback, autonomous obstacle avoidance, and sound-triggered alerts. 

This project features a live video stream, a web-based joystick for movement, and an intelligent state machine that handles sensor inputs and audio playback concurrently without freezing the camera stream.

## 🌟 Features

- **Live Video Streaming:** High-performance web interface with real-time video feed.
- **Web Joystick Control:** Omnidirectional control through a touch-friendly web joystick.
- **Interactive Audio (DFPlayer Mini):** Plays specific audio files for startup, directional movements, and alerts.
- **Intelligent Obstacle Avoidance:** Uses an IR sensor. If blocked for >500ms, the robot halts, plays an alert, and autonomously backs up while playing a reversing sound.
- **Noise Detection:** Uses a sound sensor. If loud noise is detected for >3 seconds, it triggers a UI alert and an audio warning.
- **Camera Configuration Settings:** Adjust resolution, brightness, contrast, and LED flash intensity directly from the web UI.

## 🛠️ Hardware Requirements

- **ESP32-CAM** (AI-Thinker module) + FTDI Programmer for uploading
- **PCF8574** I2C I/O Expander module (Address `0x20`)
- **Motor Driver** (e.g., L298N or L293D) + DC Motors
- **DFRobot DFPlayer Mini** + MicroSD Card
- **6W Speaker** (< 8-ohm impedance)
- **IR Obstacle Sensor**
- **Sound Sensor Module**
- Power Supply (e.g., 2-cell LiPo battery with buck converters for 5V distribution)

## 🔌 Wiring & Pinout

### ESP32-CAM -> Peripherals
| ESP32-CAM Pin | Connected To | Notes |
| :--- | :--- | :--- |
| **GPIO 14** | PCF8574 **SDA** | I2C Data |
| **GPIO 15** | PCF8574 **SCL** | I2C Clock |
| **GPIO 13** (RX) | DFPlayer **TX** | Serial Comm |
| **GPIO 2** (TX) | DFPlayer **RX** | Serial Comm *(Add a 1kΩ resistor in series to reduce noise)* |
| **5V** / **GND** | Common Power | Power distribution |

### PCF8574 I/O Expander (Address 0x20)
| PCF8574 Pin | Connected To | Function |
| :--- | :--- | :--- |
| **P0** | Motor Driver **IN1** | Right Motor FWD |
| **P1** | Motor Driver **IN2** | Right Motor REV |
| **P2** | Motor Driver **IN3** | Left Motor FWD |
| **P3** | Motor Driver **IN4** | Left Motor REV |
| **P4** | IR Sensor **OUT** | Obstacle Detection |
| **P5** | Sound Sensor **OUT**| Noise Detection |

## 🎵 DFPlayer Mini SD Card Setup

The DFPlayer Mini requires a very specific folder and naming structure to play the correct files when requested by the code. 

1. Format your MicroSD card to **FAT32**.
2. Create a folder in the root directory exactly named `mp3` (lowercase).
3. Place your audio files inside the `mp3` folder and name them as follows:

| File Name | Trigger / Action |
| :--- | :--- |
| `0001.mp3` | **System Startup** (Plays once on boot) |
| `0002.mp3` | **Obstacle Detected** (Starts autonomous backup sequence) |
| `0003.mp3` | **Noise Detected** (Plays when sound sensor is triggered > 3s) |
| `0005.mp3` | **Move Forward** (Joystick UP) |
| `0006.mp3` | **Move Backward** (Joystick DOWN or Backup Sequence) |
| `0007.mp3` | **Turn Left** (Joystick LEFT) |
| `0008.mp3` | **Turn Right** (Joystick RIGHT) |

## 💻 Software Setup & Installation

1. **Arduino IDE Configuration:**
   - Install the **ESP32 Board Package** in the Arduino IDE.
   - Select Board: **AI Thinker ESP32-CAM**.
   - Select Partition Scheme: **Huge APP (3MB No OTA/1MB SPIFFS)** *(Crucial for the camera web server to fit in memory)*.

2. **Required Libraries:**
   Install the following libraries via the Arduino Library Manager (`Sketch` -> `Include Library` -> `Manage Libraries`):
   - `PCF8574` by Renzo Mischianti
   - `DFRobotDFPlayerMini` by DFRobot

3. **Network Configuration:**
   Open `CameraWebServer.ino` and update your Wi-Fi credentials:
   ```cpp
   const char* ssid = "YOUR_WIFI_SSID";
   const char* password = "YOUR_WIFI_PASSWORD";
   ```

4. **Upload the Code:**
   - Connect your ESP32-CAM to your FTDI programmer (Ensure `GPIO 0` is connected to `GND` during boot to enter flashing mode).
   - Click **Upload**.
   - Once uploaded, disconnect `GPIO 0` from `GND` and press the reset button.

## 🚀 Usage

1. Open the Arduino IDE **Serial Monitor** (Baud rate: 115200).
2. Wait for the ESP32 to connect to Wi-Fi. It will print an IP address (e.g., `http://192.168.1.100`).
3. Ensure your phone or computer is on the same Wi-Fi network.
4. Open a web browser and enter the IP address.
5. Click **START LIVE FEED** to view the camera stream.
6. Use the on-screen joystick to drive the robot. The corresponding sounds will play automatically!

## ⚠️ Troubleshooting

- **Camera Init Failed:** Ensure the camera ribbon cable is seated securely. Check your power supply; the ESP32-CAM requires a stable 5V/2A source.
- **No Audio / Random Audio:** Double-check that your SD card is formatted to FAT32 and the files are exactly named `0001.mp3`, `0002.mp3` inside an `mp3` folder. 
- **Robot Resets when Motors run:** Motors draw high current, causing a voltage drop (brownout) on the ESP32. Power the motors directly from your battery/buck converter, NOT from the ESP32's 5V pin.

---
*Built for smart class monitoring and interactive robotics learning.*
