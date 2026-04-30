#include "esp_camera.h"
#include <WiFi.h>
#include "esp32-hal-ledc.h"
#include <Wire.h>
#include <PCF8574.h>

// WiFi credential for network
const char* ssid = "Motopri";
const char* password = "17340001";

// Sensor Pins declare here 
#define IR_PIN 2
#define SOUND_PIN 13 

int lastIR = 1;
int lastSound = 1;

// --- NON-BLOCKING TIMING VARIABLES ---
unsigned long obstacleStartTime = 0;
const unsigned long backwardDuration = 400; // Move back for 800ms
bool isBackingUp = false;

unsigned long lastSensorReadTime = 0;
const int sensorPollInterval = 25; // Read sensors every 50ms

// PCF8574
PCF8574 pcf8574(0x20);
bool pcf_ok = false;

// Motor mapping
#define IN1 0
#define IN2 1
#define IN3 2
#define IN4 3

// Camera pins (ESP32-CAM AI-Thinker)
#define PWDN_GPIO_NUM  32
#define RESET_GPIO_NUM -1
#define XCLK_GPIO_NUM  0
#define SIOD_GPIO_NUM  26
#define SIOC_GPIO_NUM  27
#define LED_GPIO_NUM   4
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

void startCameraServer();

// ===== MOTOR CONTROL function created by us  =====
void forward() { 
  if(pcf_ok){ pcf8574.write(IN1, HIGH); pcf8574.write(IN2, LOW); pcf8574.write(IN3, HIGH); pcf8574.write(IN4, LOW); } }
void backward() {
   if(pcf_ok){ pcf8574.write(IN1, LOW); pcf8574.write(IN2, HIGH); pcf8574.write(IN3, LOW); pcf8574.write(IN4, HIGH); } }
void stopMotor() { 
  if(pcf_ok){ pcf8574.write(IN1, LOW); pcf8574.write(IN2, LOW); pcf8574.write(IN3, LOW); pcf8574.write(IN4, LOW); } }
void left() {
  if(pcf_ok){
    pcf8574.write(IN1, LOW);
    pcf8574.write(IN2, LOW);
    pcf8574.write(IN3, HIGH);
    pcf8574.write(IN4, LOW);
  }
}

void right() {
  if(pcf_ok){
    pcf8574.write(IN1, HIGH);
    pcf8574.write(IN2, LOW);
    pcf8574.write(IN3, LOW);
    pcf8574.write(IN4, LOW);
  }
}
void setup() {
  Serial.begin(115200);
  Wire.begin(14, 15); 
  pcf_ok = pcf8574.begin();
  if (pcf_ok) stopMotor();

  pinMode(IR_PIN, INPUT);
  pinMode(SOUND_PIN, INPUT);

  // CAMERA CONFIG (Fixed for Stream Stability) ( copied from example code )
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
  config.xclk_freq_hz = 15000000; // Stable XCLK
  config.frame_size = FRAMESIZE_QVGA;
  config.pixel_format = PIXFORMAT_JPEG;
  config.jpeg_quality = 15;
  config.fb_count = 2;
  if (psramFound()) {
    config.fb_count = 4;
  }

  if (esp_camera_init(&config) != ESP_OK) return;

  WiFi.begin(ssid, password);
  WiFi.setSleep(false);
  while (WiFi.status() != WL_CONNECTED) { delay(500); }

  startCameraServer();

  Serial.print("📡 Stream: http://");
  Serial.println(WiFi.localIP());
  
  startCameraServer();
}

void loop() {
  // 1. joystick to handle motor state 
  extern void handleMotorState();
  
  // Only handle manual command 
  if (!isBackingUp) {
    handleMotorState();
  }

  // 2. Sensor Polling (Non-blocking Interval)
  if (millis() - lastSensorReadTime > sensorPollInterval) {
    int ir = digitalRead(IR_PIN);
    int sound = digitalRead(SOUND_PIN);

    // --- IR OBSTACLE DETECTION ---
    if (ir == 0 && lastIR == 1 && !isBackingUp) {
     
      isBackingUp = true;
      obstacleStartTime = millis();
      backward(); // Start backward movement
    }

    // --- SOUND DETECTION ---
    if (sound == 0 && lastSound == 1) {
     
    }

    lastIR = ir;
    lastSound = sound;
    lastSensorReadTime = millis();
  }

  // Automatic Backup Timer (Non-blocking)
  if (isBackingUp) {
    if (millis() - obstacleStartTime > backwardDuration) {
      stopMotor();
      isBackingUp = false;
      
    }
  }

  delay(1); 
}