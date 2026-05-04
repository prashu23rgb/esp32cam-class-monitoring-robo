#include "esp_camera.h"
#include <WiFi.h>
#include <Wire.h>
#include <PCF8574.h>
#include <DFRobotDFPlayerMini.h>

// wifi 
const char* ssid = "Motopri";
const char* password = "17340001";

// Shared state for app_httpd.cpp
int ir_state = 1;
int sound_state = 1;

// PCF8574 I/O Expander
PCF8574 pcf8574(0x20);
bool pcf_ok = false;

#define IN1 0
#define IN2 1
#define IN3 2
#define IN4 3

#define IR_PIN 4
#define SOUND_PIN 5

// DFPlayer Mini
HardwareSerial dfSerial(1);
DFRobotDFPlayerMini dfPlayer;

// Forward declarations for functions used by app_httpd.cpp
void startCameraServer();
extern void handleMotorState();

// --- MOTOR CONTROL FUNCTIONS (called by app_httpd.cpp) ---
void forward() {
  if (!pcf_ok) return;
  pcf8574.write(IN1, HIGH); pcf8574.write(IN2, LOW);
  pcf8574.write(IN3, HIGH); pcf8574.write(IN4, LOW);
}

void backward() {
  if (!pcf_ok) return;
  pcf8574.write(IN1, LOW); pcf8574.write(IN2, HIGH);
  pcf8574.write(IN3, LOW); pcf8574.write(IN4, HIGH);
}

void left() {
  if (!pcf_ok) return;
  pcf8574.write(IN1, HIGH); pcf8574.write(IN2, LOW);
  pcf8574.write(IN3, LOW); pcf8574.write(IN4, LOW);
}

void right() {
  if (!pcf_ok) return;
  pcf8574.write(IN1, LOW); pcf8574.write(IN2, LOW);
  pcf8574.write(IN3, HIGH); pcf8574.write(IN4, LOW);
}

void stopMotor() {
  if (!pcf_ok) return;
  pcf8574.write(IN1, LOW); pcf8574.write(IN2, LOW);
  pcf8574.write(IN3, LOW); pcf8574.write(IN4, LOW);
}

// --- AUDIO CONTROL FUNCTION (called by app_httpd.cpp) ---
void playMovementSound(int state) {
  switch (state) {
    case 1: dfPlayer.playMp3Folder(5); break; // Forward -> mp3/0005.mp3
    case 2: dfPlayer.playMp3Folder(6); break; // Backward -> mp3/0006.mp3
    case 3: dfPlayer.playMp3Folder(7); break; // Left -> mp3/0007.mp3
    case 4: dfPlayer.playMp3Folder(8); break; // Right -> mp3/0008.mp3
  }
}

// --- SENSOR & AUTONOMY GLOBALS ---
unsigned long lastSensorReadTime = 0;
const int sensorPollInterval = 100;

bool was_ir_blocked = false;
bool is_backing_up = false;
unsigned long backup_start_time = 0;
const int backup_duration = 500;

// ---------------- CAMERA ----------------
#define PWDN_GPIO_NUM  32
#define RESET_GPIO_NUM -1
#define XCLK_GPIO_NUM  0
#define SIOD_GPIO_NUM  26
#define SIOC_GPIO_NUM  27
#define Y9_GPIO_NUM    35
#define Y8_GPIO_NUM    34
#define Y7_GPIO_NUM    39
#define Y6_GPIO_NUM    36
#define Y5_GPIO_NUM    21
#define Y4_GPIO_NUM    19
#define Y3_GPIO_NUM    18
#define Y2_GPIO_NUM    5
#define VSYNC_GPIO_NUM 25
#define HREF_GPIO_NUM  23
#define PCLK_GPIO_NUM  22

// ---------------- SETUP ----------------
void setup() {
  Serial.begin(115200);

  Wire.begin(14, 15);
  pcf_ok = pcf8574.begin();

  if (pcf_ok) {
    stopMotor();
    // For PCF8574, writing HIGH to a pin sets it to high-impedance (input mode)
    pcf8574.write(IR_PIN, HIGH);
    pcf8574.write(SOUND_PIN, HIGH);
  }

  // DFPlayer Mini
  dfSerial.begin(9600, SERIAL_8N1, 13, 2);

  if (dfPlayer.begin(dfSerial, /*isACK*/ false, /*doReset*/ true)) {
    Serial.println("DFPlayer Mini online.");
    dfPlayer.volume(25);
    dfPlayer.playMp3Folder(1); // Play startup sound mp3/0001.mp3
  } else {
    Serial.println("DFPlayer Mini failed to start.");
  }

  // CAMERA CONFIG
  camera_config_t config;
  config.ledc_channel = LEDC_CHANNEL_0;
  config.ledc_timer = LEDC_TIMER_0;
  config.pin_d0 = Y2_GPIO_NUM;
  config.pin_d1 = Y3_GPIO_NUM;
  config.pin_d2 = Y4_GPIO_NUM;
  config.pin_d3 = Y5_GPIO_NUM;
  config.pin_d4 = Y6_GPIO_NUM;
  config.pin_d5 = Y7_GPIO_NUM;
  config.pin_d6 = Y8_GPIO_NUM;
  config.pin_d7 = Y9_GPIO_NUM;
  config.pin_xclk = XCLK_GPIO_NUM;
  config.pin_pclk = PCLK_GPIO_NUM;
  config.pin_vsync = VSYNC_GPIO_NUM;
  config.pin_href = HREF_GPIO_NUM;
  config.pin_sccb_sda = SIOD_GPIO_NUM;
  config.pin_sccb_scl = SIOC_GPIO_NUM;
  config.pin_pwdn = PWDN_GPIO_NUM;
  config.pin_reset = RESET_GPIO_NUM;
  config.xclk_freq_hz = 15000000;
  config.frame_size = FRAMESIZE_QVGA;
  config.pixel_format = PIXFORMAT_JPEG;
  config.jpeg_quality = 15;
  config.fb_count = psramFound() ? 4 : 2;

  if (esp_camera_init(&config) != ESP_OK) return;

  WiFi.begin(ssid, password);

  unsigned long startAttempt = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - startAttempt < 10000) {
    delay(300);
  }

  if (WiFi.status() == WL_CONNECTED) {
    Serial.println(WiFi.localIP());
  }

  startCameraServer();
  Serial.println("Robot ready.");
}

// ---------------- LOOP ----------------
void loop() {
  // Let the web server handle motor commands and timeouts
  handleMotorState();

  // Handle autonomous backup state
  if (is_backing_up && millis() - backup_start_time > backup_duration) {
    stopMotor();
    is_backing_up = false;
  }

  // Poll sensors periodically
  if (millis() - lastSensorReadTime > sensorPollInterval) {
    lastSensorReadTime = millis();

    if (!pcf_ok) return;

    ir_state = pcf8574.read(IR_PIN);
    sound_state = pcf8574.read(SOUND_PIN);

    // IR Obstacle Logic: Trigger only on the transition from clear to blocked
    if (ir_state == 0 && !was_ir_blocked && !is_backing_up) {
      was_ir_blocked = true; // Mark as blocked to prevent re-triggering
      is_backing_up = true;   // Enter backup state
      backup_start_time = millis();

      Serial.println("Obstacle detected! Initiating backup sequence.");
      
      stopMotor(); // Stop current movement immediately
      
      // Play sequence: 0002.mp3 -> delay -> 0006.mp3
      dfPlayer.playMp3Folder(2);
      // IMPORTANT: Adjust this delay to match the length of your 0002.mp3 file in milliseconds.
      // A better method is to use the BUSY pin of the DFPlayer to know when a track is finished.
      delay(1500); 
      
      dfPlayer.playMp3Folder(6);
      
      // Start moving backward
      backward();
    } else if (ir_state != 0) {
      was_ir_blocked = false; // Path is clear, reset the flag
    }
  }
}