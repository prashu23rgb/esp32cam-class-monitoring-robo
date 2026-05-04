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
  pcf8574.write(IN1, LOW); pcf8574.write(IN2, LOW);
  pcf8574.write(IN3, HIGH); pcf8574.write(IN4, LOW);
}

void right() {
  if (!pcf_ok) return;
  pcf8574.write(IN1, HIGH); pcf8574.write(IN2, LOW);
  pcf8574.write(IN3, LOW); pcf8574.write(IN4, LOW);
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
bool was_sound_detected = false;
unsigned long ir_block_start = 0;
unsigned long sound_block_start = 0;
bool is_backing_up = false;
int backup_state = 0;
unsigned long backup_timer = 0;
const int audio2_duration = 2500; // IMPORTANT: Set this to the exact length of 0002.mp3 in ms
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
    delay(2000); // Give the DFPlayer time to mount the SD card after resetting
    dfPlayer.volume(25);
    delay(100);
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
  if (!is_backing_up) {
    handleMotorState();
  }

  // Handle autonomous backup state machine without blocking delays!
  if (is_backing_up) {
    if (backup_state == 1 && millis() - backup_timer > audio2_duration) {
      // Step 1 complete (Audio 2 finished). Start Step 2: Audio 6 + Move Backward
      dfPlayer.playMp3Folder(6);
      backward();
      backup_state = 2;
      backup_timer = millis();
    } else if (backup_state == 2 && millis() - backup_timer > backup_duration) {
      // Step 2 complete (Backup movement finished). Stop and return to normal.
      stopMotor();
      is_backing_up = false;
      backup_state = 0;
    }
  }

  // Poll sensors periodically
  if (millis() - lastSensorReadTime > sensorPollInterval) {
    lastSensorReadTime = millis();

    if (!pcf_ok) return;

    int raw_ir = pcf8574.read(IR_PIN);
    int raw_sound = pcf8574.read(SOUND_PIN);

    // IR Obstacle Logic: Trigger only if blocked continuously for > 500ms
    if (raw_ir == 0) {
      if (ir_block_start == 0) ir_block_start = millis(); // Start timing
      if (millis() - ir_block_start > 500 && !was_ir_blocked && !is_backing_up) {
        was_ir_blocked = true; // Mark as blocked to prevent re-triggering
        ir_state = 0;          // Expose blocked state to web UI
        is_backing_up = true;
        backup_state = 1;
        backup_timer = millis();

        Serial.println("Obstacle detected! Initiating backup sequence.");
        
        stopMotor(); // Stop current movement immediately
        dfPlayer.playMp3Folder(2); // Start Step 1: Play Audio 2
      }
    } else {
      ir_block_start = 0;      // Reset timer
      was_ir_blocked = false;  // Path is clear
      ir_state = 1;            // Expose clear state to web UI
    }

    // Sound Sensor Logic: Trigger only if noise detected continuously for > 3000ms
    if (raw_sound == 0) {
      if (sound_block_start == 0) sound_block_start = millis(); // Start timing
      if (millis() - sound_block_start > 3000 && !was_sound_detected && !is_backing_up) {
        was_sound_detected = true;
        sound_state = 0;       // Expose noise state to web UI
        Serial.println("Noise detected! Playing audio 3.");
        dfPlayer.playMp3Folder(3);
      }
    } else {
      sound_block_start = 0;       // Reset timer
      was_sound_detected = false;  // Reset detection
      sound_state = 1;             // Expose quiet state to web UI
    }
  }
}