#include "esp_camera.h"
#include <WiFi.h>
#include "esp32-hal-ledc.h"
#include <Wire.h>
#include <PCF8574.h>
#include <DFRobotDFPlayerMini.h>

// wifi 
const char* ssid = "Motopri";
const char* password = "17340001";

// sensor state declare 
int ir_state = 1;
int sound_state = 1;

// pcf
PCF8574 pcf8574(0x20);
bool pcf_ok = false;

#define IN1 0
#define IN2 1
#define IN3 2
#define IN4 3

#define IR_PIN 4
#define SOUND_PIN 5

//  DF PLAYER 
HardwareSerial dfSerial(1);
DFRobotDFPlayerMini dfPlayer;


bool isAudioPlaying = false;
unsigned long audioStartTime = 0;
const int audioDuration = 1500;

void playSound(int id) {
  if (isAudioPlaying) return;
  dfPlayer.play(id);
  isAudioPlaying = true;
  audioStartTime = millis();
}

// motor cmd 
int currentCommand = 0;
unsigned long commandStartTime = 0;
const int commandDuration = 200;

void executeMotor(int cmd) {
  if (!pcf_ok) return;

  switch (cmd) {
    case 1:
      pcf8574.write(IN1, HIGH); pcf8574.write(IN2, LOW);
      pcf8574.write(IN3, HIGH); pcf8574.write(IN4, LOW);
      playSound(5);
      break;

    case 2:
      pcf8574.write(IN1, LOW); pcf8574.write(IN2, HIGH);
      pcf8574.write(IN3, LOW); pcf8574.write(IN4, HIGH);
      playSound(6);
      break;

    case 3:
      pcf8574.write(IN1, HIGH); pcf8574.write(IN2, LOW);
      pcf8574.write(IN3, LOW); pcf8574.write(IN4, LOW);
      playSound(7);
      break;

    case 4:
      pcf8574.write(IN1, LOW); pcf8574.write(IN2, LOW);
      pcf8574.write(IN3, HIGH); pcf8574.write(IN4, LOW);
      playSound(8);
      break;
  }
}

void stopMotor() {
  if (!pcf_ok) return;
  pcf8574.write(IN1, LOW);
  pcf8574.write(IN2, LOW);
  pcf8574.write(IN3, LOW);
  pcf8574.write(IN4, LOW);
}

void triggerCommand(int cmd) {
  currentCommand = cmd;
  commandStartTime = millis();
  executeMotor(cmd);
}

// ---------------- SENSOR ----------------
unsigned long lastSensorReadTime = 0;
const int sensorPollInterval = 25;

unsigned long irStart = 0;
bool obstacleHandled = false;

bool isBackingUp = false;
unsigned long obstacleStartTime = 0;
const int backwardDuration = 400;

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

void startCameraServer();

// ---------------- SETUP ----------------
void setup() {
  Serial.begin(115200);

  Wire.begin(14, 15);
  pcf_ok = pcf8574.begin();

  if (pcf_ok) {
    stopMotor();
    pcf8574.write(IR_PIN, HIGH);
    pcf8574.write(SOUND_PIN, HIGH);
  }

  // DFPlayer
  dfSerial.begin(9600, SERIAL_8N1, 13, 2);

  if (dfPlayer.begin(dfSerial)) {
    Serial.println("DF OK");
    delay(1500);
    dfPlayer.volume(25);
    dfPlayer.play(1);
  } else {
    Serial.println("DF FAIL");
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
}

// ---------------- LOOP ----------------
void loop() {

  extern int robotState;

  //cmd
  if (robotState != 0 && !isBackingUp) {
    triggerCommand(robotState);
    robotState = 0;
  }

  // auto stop 
  if (currentCommand != 0 && millis() - commandStartTime > commandDuration) {
    stopMotor();
    currentCommand = 0;
  }

  // audio reset 
  if (isAudioPlaying && millis() - audioStartTime > audioDuration) {
    isAudioPlaying = false;
  }

  // polling 
  if (millis() - lastSensorReadTime > sensorPollInterval) {

    int ir = pcf8574.read(IR_PIN);
    int sound = pcf8574.read(SOUND_PIN);

    // update shared state (THIS WAS MISSING)
    ir_state = ir;
    sound_state = sound;

    // IR obstacle logic
    if (ir == 0) {
      if (!obstacleHandled) {

        if (irStart == 0) irStart = millis();

        if (millis() - irStart > 100) {
          isBackingUp = true;
          obstacleStartTime = millis();

          triggerCommand(2);
          playSound(2);

          obstacleHandled = true;
        }
      }
    } else {
      irStart = 0;
      obstacleHandled = false;
    }

    lastSensorReadTime = millis();
  }

  //  BACKWARD STOP
  if (isBackingUp && millis() - obstacleStartTime > backwardDuration) {
    stopMotor();
    isBackingUp = false;
  }
}