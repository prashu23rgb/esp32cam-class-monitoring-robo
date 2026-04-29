// Copyright to me 
#include "esp_http_server.h"
#include "esp_timer.h"
#include "esp_camera.h"
#include "img_converters.h"
#include "fb_gfx.h"
#include "esp32-hal-ledc.h"
#include "sdkconfig.h"
#include "Arduino.h" 

#if defined(ARDUINO_ARCH_ESP32) && defined(CONFIG_ARDUHAL_ESP_LOG)
#include "esp32-hal-log.h"
#endif

// --- EXTERNAL MOTOR FUNCTIONS ---
extern void forward();
extern void backward();
extern void left();
extern void right();
extern void stopMotor();

// --- STATE MACHINE GLOBALS ---
unsigned long lastCmdTime = 0;
int robotState = 0; // 0:Stop, 1:FWD, 2:BWD, 3:LEFT, 4:RIGHT

// Called by loop() in main.ino
void handleMotorState() {
  // Safety Heartbeat: Stop if no command for 400ms
  if (robotState != 0 && (millis() - lastCmdTime > 400)) {
    robotState = 0;
  }
  switch(robotState) {
    case 1: forward(); break;
    case 2: backward(); break;
    case 3: left(); break;
    case 4: right(); break;
    default: stopMotor(); break;
  }
}

#if defined(LED_GPIO_NUM)
#define CONFIG_LED_MAX_INTENSITY 255
int led_duty = 0;
bool isStreaming = false;
void enable_led(bool en) {
  int duty = en ? led_duty : 0;
  if (en && isStreaming && (led_duty > CONFIG_LED_MAX_INTENSITY)) duty = CONFIG_LED_MAX_INTENSITY;
  ledcWrite(LED_GPIO_NUM, duty);
}
#endif

typedef struct {
  httpd_req_t *req;
  size_t len;
} jpg_chunking_t;

#define PART_BOUNDARY "123456789000000000000987654321"
static const char *_STREAM_CONTENT_TYPE = "multipart/x-mixed-replace;boundary=" PART_BOUNDARY;
static const char *_STREAM_BOUNDARY = "\r\n--" PART_BOUNDARY "\r\n";
static const char *_STREAM_PART = "Content-Type: image/jpeg\r\nContent-Length: %u\r\nX-Timestamp: %d.%06d\r\n\r\n";

httpd_handle_t stream_httpd = NULL;
httpd_handle_t camera_httpd = NULL;

// --- UTILITY HANDLERS ---

static size_t jpg_encode_stream(void *arg, size_t index, const void *data, size_t len) {
  jpg_chunking_t *j = (jpg_chunking_t *)arg;
  if (!index) j->len = 0;
  if (httpd_resp_send_chunk(j->req, (const char *)data, len) != ESP_OK) return 0;
  j->len += len;
  return len;
}

static esp_err_t parse_get(httpd_req_t *req, char **obuf) {
  char *buf = NULL;
  size_t buf_len = httpd_req_get_url_query_len(req) + 1;
  if (buf_len > 1) {
    buf = (char *)malloc(buf_len);
    if (!buf) { httpd_resp_send_500(req); return ESP_FAIL; }
    if (httpd_req_get_url_query_str(req, buf, buf_len) == ESP_OK) { *obuf = buf; return ESP_OK; }
    free(buf);
  }
  httpd_resp_send_404(req);
  return ESP_FAIL;
}

// --- MAIN HANDLERS ---

static esp_err_t stream_handler(httpd_req_t *req) {
  camera_fb_t *fb = NULL;
  struct timeval _timestamp;
  esp_err_t res = ESP_OK;
  size_t _jpg_buf_len = 0;
  uint8_t *_jpg_buf = NULL;
  char *part_buf[128];

  res = httpd_resp_set_type(req, _STREAM_CONTENT_TYPE);
  if (res != ESP_OK) return res;
  httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");

#if defined(LED_GPIO_NUM)
  isStreaming = true;
  enable_led(true);
#endif

  while (true) {
    fb = esp_camera_fb_get();
    if (!fb) { res = ESP_FAIL; } 
    else {
      _timestamp.tv_sec = fb->timestamp.tv_sec;
      _timestamp.tv_usec = fb->timestamp.tv_usec;
      _jpg_buf_len = fb->len;
      _jpg_buf = fb->buf;
    }

    if (res == ESP_OK) res = httpd_resp_send_chunk(req, _STREAM_BOUNDARY, strlen(_STREAM_BOUNDARY));
    if (res == ESP_OK) {
      size_t hlen = snprintf((char *)part_buf, 128, _STREAM_PART, _jpg_buf_len, _timestamp.tv_sec, _timestamp.tv_usec);
      res = httpd_resp_send_chunk(req, (const char *)part_buf, hlen);
    }
    if (res == ESP_OK) res = httpd_resp_send_chunk(req, (const char *)_jpg_buf, _jpg_buf_len);
    
    if (fb) { esp_camera_fb_return(fb); fb = NULL; _jpg_buf = NULL; }
    if (res != ESP_OK) break;
  }

#if defined(LED_GPIO_NUM)
  isStreaming = false;
  enable_led(false);
#endif
  return res;
}

static esp_err_t cmd_handler(httpd_req_t *req) {
  char *buf = NULL;
  char variable[32];
  char value[32];

  if (parse_get(req, &buf) != ESP_OK) return ESP_FAIL;
  if (httpd_query_key_value(buf, "var", variable, sizeof(variable)) != ESP_OK || 
      httpd_query_key_value(buf, "val", value, sizeof(value)) != ESP_OK) {
    free(buf);
    httpd_resp_send_404(req);
    return ESP_FAIL;
  }
  free(buf);

  int val = atoi(value);
  sensor_t *s = esp_camera_sensor_get();
  int res = 0;

  // Optimized Motor Logic with Heartbeat
  if (!strcmp(variable, "go")) {
    robotState = val;
    lastCmdTime = millis(); 
  } 
  // FULL Camera Settings preserved
  else if (!strcmp(variable, "framesize")) { if (s->pixformat == PIXFORMAT_JPEG) res = s->set_framesize(s, (framesize_t)val); }
  else if (!strcmp(variable, "quality")) res = s->set_quality(s, val);
  else if (!strcmp(variable, "contrast")) res = s->set_contrast(s, val);
  else if (!strcmp(variable, "brightness")) res = s->set_brightness(s, val);
  else if (!strcmp(variable, "saturation")) res = s->set_saturation(s, val);
  else if (!strcmp(variable, "gainceiling")) res = s->set_gainceiling(s, (gainceiling_t)val);
  else if (!strcmp(variable, "colorbar")) res = s->set_colorbar(s, val);
  else if (!strcmp(variable, "awb")) res = s->set_whitebal(s, val);
  else if (!strcmp(variable, "agc")) res = s->set_gain_ctrl(s, val);
  else if (!strcmp(variable, "aec")) res = s->set_exposure_ctrl(s, val);
  else if (!strcmp(variable, "hmirror")) res = s->set_hmirror(s, val);
  else if (!strcmp(variable, "vflip")) res = s->set_vflip(s, val);
  else if (!strcmp(variable, "awb_gain")) res = s->set_awb_gain(s, val);
  else if (!strcmp(variable, "agc_gain")) res = s->set_agc_gain(s, val);
  else if (!strcmp(variable, "aec_value")) res = s->set_aec_value(s, val);
  else if (!strcmp(variable, "aec2")) res = s->set_aec2(s, val);
  else if (!strcmp(variable, "dcw")) res = s->set_dcw(s, val);
  else if (!strcmp(variable, "bpc")) res = s->set_bpc(s, val);
  else if (!strcmp(variable, "wpc")) res = s->set_wpc(s, val);
  else if (!strcmp(variable, "raw_gma")) res = s->set_raw_gma(s, val);
  else if (!strcmp(variable, "lenc")) res = s->set_lenc(s, val);
  else if (!strcmp(variable, "special_effect")) res = s->set_special_effect(s, val);
  else if (!strcmp(variable, "wb_mode")) res = s->set_wb_mode(s, val);
  else if (!strcmp(variable, "ae_level")) res = s->set_ae_level(s, val);
#if defined(LED_GPIO_NUM)
  else if (!strcmp(variable, "led_intensity")) { led_duty = val; if (isStreaming) enable_led(true); }
#endif
  else { res = -1; }

  if (res < 0) return httpd_resp_send_500(req);
  httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
  return httpd_resp_send(req, NULL, 0);
}

static esp_err_t status_handler(httpd_req_t *req) {
  static char json_response[1024];
  sensor_t *s = esp_camera_sensor_get();
  char *p = json_response;
  *p++ = '{';
  p += sprintf(p, "\"framesize\":%u,", s->status.framesize);
  p += sprintf(p, "\"quality\":%u,", s->status.quality);
  p += sprintf(p, "\"brightness\":%d,", s->status.brightness);
  p += sprintf(p, "\"contrast\":%d,", s->status.contrast);
  p += sprintf(p, "\"saturation\":%d,", s->status.saturation);
  p += sprintf(p, "\"hmirror\":%u,", s->status.hmirror);
  p += sprintf(p, "\"vflip\":%u,", s->status.vflip);
  p += sprintf(p, "\"colorbar\":%u", s->status.colorbar);
#if defined(LED_GPIO_NUM)
  p += sprintf(p, ",\"led_intensity\":%u", led_duty);
#endif
  *p++ = '}';
  *p++ = 0;
  httpd_resp_set_type(req, "application/json");
  httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
  return httpd_resp_send(req, json_response, strlen(json_response));
}static esp_err_t index_handler(httpd_req_t *req) {
  httpd_resp_set_type(req, "text/html");
  const char* html = 
"<!DOCTYPE html><html><head><meta charset='UTF-8'><meta name='viewport' content='width=device-width,initial-scale=1,maximum-scale=1,user-scalable=no'>"
"<title>ROBO ERIC PRO</title>"
"<script src='https://cdnjs.cloudflare.com/ajax/libs/nipplejs/0.9.0/nipplejs.min.js'></script>"
"<style>"
"body{font-family:sans-serif;background:#121212;color:#fff;margin:0;display:flex;flex-direction:column;height:100vh;overflow:hidden}"
".header{background:#00bcd4;width:100%;padding:12px 0;text-align:center;box-shadow:0 2px 10px rgba(0,0,0,0.5);font-weight:bold;text-transform:uppercase;z-index:10}"
".main-layout{display:flex;flex:1;width:100%;overflow:hidden}"
".side-panel{width:340px;background:#1e1e1e;padding:15px;display:flex;flex-direction:column;gap:12px;border-right:1px solid #333;box-sizing:border-box;overflow-y:auto}"
".v-container{position:relative;width:100%;background:#000;border-radius:12px;overflow:hidden;aspect-ratio:4/3;box-shadow:0 5px 15px rgba(0,0,0,0.8)}"
"#stream{width:100%;height:100%;object-fit:contain;transform:rotate(180deg)}"
".btn-stream{width:100%;padding:12px;background:#2ecc71;border:none;border-radius:8px;color:#fff;font-weight:bold;cursor:pointer;margin-bottom:5px}"
".settings-group{background:#252525;padding:12px;border-radius:10px;display:grid;grid-template-columns:1fr 1.2fr;gap:10px;align-items:center;font-size:13px}"
"h4{grid-column:span 2;margin:0 0 5px 0;color:#00bcd4;font-size:11px;text-transform:uppercase;letter-spacing:1px}"
"select,input[type=range]{width:100%;background:#333;color:#fff;border:1px solid #444;padding:4px;border-radius:5px;accent-color:#00bcd4}"
".switch-group{grid-column:span 2;display:flex;justify-content:space-between;gap:5px;margin-top:5px}"
".switch-group button{flex:1;padding:8px;background:#444;color:#fff;border:none;border-radius:5px;font-size:11px;cursor:pointer}"
".switch-group button.active{background:#00bcd4;color:#000}"
".control-area{flex:1;display:flex;flex-direction:column;align-items:center;justify-content:center;background:radial-gradient(circle, #1e1e1e 0%, #121212 100%)}"
"#joystick-container{width:220px;height:220px;background:rgba(255,255,255,0.02);border:2px dashed #444;border-radius:50%;position:relative;touch-action:none}"
"</style></head><body>"
"<div class='header'>Smart Class Monitoring ROBO ERIC</div>"
"<div class='main-layout'>"
  "<div class='side-panel'>"
    "<div class='v-container'><img src='' id='stream' alt='Live Feed Ready'></div>"
    "<button class='btn-stream' onclick='startStream()'>START LIVE FEED</button>"
    "<div class='settings-group'>"
      "<h4>Image Settings</h4>"
      "<span>Resolution</span><select onchange='updateCfg(\"framesize\",this.value)'><option value='5'>QVGA</option><option value='6'>CIF</option><option value='8' selected>VGA</option></select>"
      "<span>Quality</span><input type='range' min='10' max='63' value='12' onchange='updateCfg(\"quality\",this.value)'>"
      "<span>Brightness</span><input type='range' min='-2' max='2' value='0' onchange='updateCfg(\"brightness\",this.value)'>"
      "<span>Contrast</span><input type='range' min='-2' max='2' value='0' onchange='updateCfg(\"contrast\",this.value)'>"
      "<span>Saturation</span><input type='range' min='-2' max='2' value='0' onchange='updateCfg(\"saturation\",this.value)'>"
      "<span>LED Lamp</span><input type='range' min='0' max='255' value='0' onchange='updateCfg(\"led_intensity\",this.value)'>"
      "<div class='switch-group'>"
        "<button id='hmirror' onclick='toggle(\"hmirror\")'>H-Mirror</button>"
        "<button id='vflip' onclick='toggle(\"vflip\")'>V-Flip</button>"
        "<button id='colorbar' onclick='toggle(\"colorbar\")'>Colorbar</button>"
      "</div>"
    "</div>"
  "</div>"
  "<div class='control-area'>"
    "<h3 style='color:#555;margin-bottom:30px'>DRIVE CONTROL</h3>"
    "<div id='joystick-container'></div>"
  "</div>"
"</div>"
"<script>"
"const host = `http://${window.location.hostname}`;"
"let config = { hmirror:0, vflip:0, colorbar:0 };"
"function updateCfg(varName, val){ fetch(`${host}/control?var=${varName}&val=${val}`).catch(e=>{}) }"
"function toggle(varName){"
"  config[varName] = config[varName] === 0 ? 1 : 0;"
"  document.getElementById(varName).classList.toggle('active');"
"  updateCfg(varName, config[varName]);"
"}"
"function startStream(){ document.getElementById('stream').src=`${host}:81/stream` }"
"window.onload = function() {"
"  const joy = nipplejs.create({ zone: document.getElementById('joystick-container'), mode: 'static', position: {left: '50%', top: '50%'}, color: '#00bcd4', size: 160 });"
"  let last = 0;"
"  joy.on('move', (evt, data) => {"
"    if (!data.direction) return;"
"    let c = 0; const a = data.direction.angle;"
"    if(a==='up') c=1; else if(a==='down') c=2; else if(a==='left') c=3; else if(a==='right') c=4;"
"    if(c!==last){ updateCfg('go', c); last = c; }"
"  });"
"  joy.on('end', () => { updateCfg('go', 0); last = 0; });"
"};"
"</script></body></html>";
  return httpd_resp_send(req, html, strlen(html));
}
void startCameraServer() {
  httpd_config_t config = HTTPD_DEFAULT_CONFIG();
  config.max_uri_handlers = 16;
  httpd_uri_t index_uri = { .uri = "/", .method = HTTP_GET, .handler = index_handler, .user_ctx = NULL };
  httpd_uri_t status_uri = { .uri = "/status", .method = HTTP_GET, .handler = status_handler, .user_ctx = NULL };
  httpd_uri_t cmd_uri = { .uri = "/control", .method = HTTP_GET, .handler = cmd_handler, .user_ctx = NULL };
  httpd_uri_t stream_uri = { .uri = "/stream", .method = HTTP_GET, .handler = stream_handler, .user_ctx = NULL };

  if (httpd_start(&camera_httpd, &config) == ESP_OK) {
    httpd_register_uri_handler(camera_httpd, &index_uri);
    httpd_register_uri_handler(camera_httpd, &cmd_uri);
    httpd_register_uri_handler(camera_httpd, &status_uri);
  }

  config.server_port += 1;
  config.ctrl_port += 1;
  if (httpd_start(&stream_httpd, &config) == ESP_OK) {
    httpd_register_uri_handler(stream_httpd, &stream_uri);
  }
}

void setupLedFlash() {
#if defined(LED_GPIO_NUM)
  ledcAttach(LED_GPIO_NUM, 5000, 8);
#endif
}