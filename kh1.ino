
#include "esp_camera.h"
#include <WiFi.h>
#include <WebServer.h>
#include "FS.h"
#include "SD_MMC.h"

// ==========================================
// MASTER WIFI CONNECTION
// ==========================================

const char* ssid = "KATSEYE";
const char* password = "ICONIC123";

// ==========================================
// CAMERA CLIENT IP
// ==========================================

IPAddress local_IP(192, 168, 4, 10);
IPAddress gateway(192, 168, 4, 1);
IPAddress subnet(255, 255, 255, 0);

// ==========================================
// CAMERA WEB SERVER
// ==========================================

WebServer cameraServer(80);

// ==========================================
// AI THINKER ESP32-CAM PIN CONFIGURATION
// ==========================================

#define PWDN_GPIO_NUM     32
#define RESET_GPIO_NUM    -1
#define XCLK_GPIO_NUM      0
#define SIOD_GPIO_NUM     26
#define SIOC_GPIO_NUM     27

#define Y9_GPIO_NUM       35
#define Y8_GPIO_NUM       34
#define Y7_GPIO_NUM       39
#define Y6_GPIO_NUM       36
#define Y5_GPIO_NUM       21
#define Y4_GPIO_NUM       19
#define Y3_GPIO_NUM       18
#define Y2_GPIO_NUM        5

#define VSYNC_GPIO_NUM    25
#define HREF_GPIO_NUM     23
#define PCLK_GPIO_NUM     22

// ==========================================
// PHOTO FILES
// ==========================================

const char* entryFile = "/latest_entry.jpg";
const char* exitFile = "/latest_exit.jpg";

bool cameraReady = false;
bool sdReady = false;

// ==========================================
// CAMERA INITIALIZATION
// ==========================================

bool startCamera() {

  camera_config_t config = {};

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

  config.xclk_freq_hz = 20000000;

  config.pixel_format = PIXFORMAT_JPEG;

  config.frame_size = FRAMESIZE_VGA;

  config.jpeg_quality = 12;

  config.fb_count = psramFound() ? 2 : 1;

  config.grab_mode = CAMERA_GRAB_LATEST;

  Serial.println("INITIALIZING CAMERA...");

  if (psramFound()) {
    Serial.println("PSRAM FOUND!");
  } else {
    Serial.println("PSRAM NOT FOUND!");
  }

  esp_err_t result = esp_camera_init(&config);

  if (result != ESP_OK) {

    Serial.printf(
      "CAMERA INITIALIZATION ERROR: 0x%x\n",
      result
    );

    return false;
  }

  sensor_t *sensor = esp_camera_sensor_get();

  if (sensor != NULL) {

    Serial.print("CAMERA SENSOR PID: 0x");

    Serial.println(sensor->id.PID, HEX);

    // OV3660 IMAGE SETTINGS

    sensor->set_brightness(sensor, 0);

    sensor->set_contrast(sensor, 0);

    sensor->set_saturation(sensor, 0);

  }

  Serial.println("CAMERA INITIALIZED SUCCESSFULLY!");

  return true;
}

// ==========================================
// MICRO SD CARD INITIALIZATION
// ==========================================

bool startSDCard() {

  Serial.println("STARTING MICRO SD CARD...");

  // 1-BIT MODE

  if (!SD_MMC.begin("/sdcard", true)) {

    Serial.println("SD CARD MOUNT FAILED!");

    return false;
  }

  if (SD_MMC.cardType() == CARD_NONE) {

    Serial.println("NO SD CARD DETECTED!");

    return false;
  }

  Serial.println("SD CARD READY!");

  Serial.print("CARD SIZE: ");

  Serial.print(
    SD_MMC.cardSize() / (1024 * 1024)
  );

  Serial.println(" MB");

  return true;
}

// ==========================================
// CAPTURE AND SAVE PHOTO
// ==========================================

void handleCapture() {

  if (!cameraReady) {

    cameraServer.send(
      503,
      "text/plain",
      "CAMERA NOT READY"
    );

    return;
  }

  if (!sdReady) {

    cameraServer.send(
      503,
      "text/plain",
      "SD CARD NOT READY"
    );

    return;
  }

  String type = cameraServer.arg("type");

  const char* filename;

  if (type == "entry") {

    filename = entryFile;

  } else if (type == "exit") {

    filename = exitFile;

  } else {

    cameraServer.send(
      400,
      "text/plain",
      "INVALID TYPE"
    );

    return;
  }

  Serial.println("==========================");

  Serial.print("CAPTURING: ");

  Serial.println(type);

  // TAKE PICTURE

  camera_fb_t *fb = esp_camera_fb_get();

  if (!fb) {

    Serial.println("CAMERA CAPTURE FAILED!");

    cameraServer.send(
      500,
      "text/plain",
      "CAMERA CAPTURE FAILED"
    );

    return;
  }

  size_t photoSize = fb->len;

  Serial.print("PHOTO SIZE: ");

  Serial.println(photoSize);

  // DELETE OLD PHOTO

  if (SD_MMC.exists(filename)) {

    SD_MMC.remove(filename);

  }

  // SAVE NEW PHOTO

  File file = SD_MMC.open(
    filename,
    FILE_WRITE
  );

  if (!file) {

    esp_camera_fb_return(fb);

    Serial.println("FILE OPEN FAILED!");

    cameraServer.send(
      500,
      "text/plain",
      "FILE OPEN FAILED"
    );

    return;
  }

  size_t written = file.write(
    fb->buf,
    fb->len
  );

  file.close();

  esp_camera_fb_return(fb);

  if (written != photoSize) {

    Serial.println("PHOTO SAVE FAILED!");

    cameraServer.send(
      500,
      "text/plain",
      "PHOTO SAVE FAILED"
    );

    return;
  }

  Serial.println("PHOTO SAVED SUCCESSFULLY!");

  Serial.println(filename);

  Serial.println("==========================");

  cameraServer.send(
    200,
    "text/plain",
    "PHOTO CAPTURED AND SAVED SUCCESSFULLY!"
  );

}

// ==========================================
// SEND PHOTO TO WEBSITE
// ==========================================

void handleLatest() {

  String type = cameraServer.arg("type");

  const char* filename;

  if (type == "entry") {

    filename = entryFile;

  } else if (type == "exit") {

    filename = exitFile;

  } else {

    cameraServer.send(
      400,
      "text/plain",
      "INVALID PHOTO TYPE"
    );

    return;
  }

  if (!sdReady || !SD_MMC.exists(filename)) {

    cameraServer.send(
      404,
      "text/plain",
      "NO PHOTO AVAILABLE"
    );

    return;
  }

  File file = SD_MMC.open(
    filename,
    FILE_READ
  );

  if (!file) {

    cameraServer.send(
      500,
      "text/plain",
      "PHOTO OPEN FAILED"
    );

    return;
  }

  cameraServer.sendHeader(
    "Cache-Control",
    "no-store, no-cache, must-revalidate"
  );

  cameraServer.sendHeader(
    "Access-Control-Allow-Origin",
    "*"
  );

  cameraServer.streamFile(
    file,
    "image/jpeg"
  );

  file.close();

}

// ==========================================
// CAMERA STATUS
// ==========================================

void handleStatus() {

  String json = "{";

  json += "\"wifi\":";

  json +=
    WiFi.status() == WL_CONNECTED
    ? "true" : "false";

  json += ",\"camera\":";

  json +=
    cameraReady ? "true" : "false";

  json += ",\"sd\":";

  json +=
    sdReady ? "true" : "false";

  json += "}";

  cameraServer.sendHeader(
    "Access-Control-Allow-Origin",
    "*"
  );

  cameraServer.send(
    200,
    "application/json",
    json
  );

}

// ==========================================
// SETUP
// ==========================================

void setup() {

  Serial.begin(115200);

  delay(1500);

  Serial.println();

  Serial.println("==========================");

  Serial.println("KATSEYE CAMERA STARTING");

  Serial.println("==========================");

  // START CAMERA

  cameraReady = startCamera();

  // START SD CARD

  sdReady = startSDCard();

  // CONNECT TO MASTER WIFI

  WiFi.mode(WIFI_STA);

  WiFi.setSleep(false);

  WiFi.config(
    local_IP,
    gateway,
    subnet
  );

  WiFi.begin(
    ssid,
    password
  );

  Serial.println("CONNECTING TO MASTER ESP32...");

  unsigned long startTime = millis();

  while (
    WiFi.status() != WL_CONNECTED &&
    millis() - startTime < 20000
  ) {

    delay(500);

    Serial.print(".");

  }

  Serial.println();

  if (WiFi.status() == WL_CONNECTED) {

    Serial.println("CONNECTED TO MASTER!");

    Serial.print("CAMERA IP: ");

    Serial.println(WiFi.localIP());

  } else {

    Serial.println("MASTER WIFI CONNECTION FAILED!");

  }

  // CAMERA SERVER ROUTES

  cameraServer.on(
    "/capture",
    HTTP_GET,
    handleCapture
  );

  cameraServer.on(
    "/latest",
    HTTP_GET,
    handleLatest
  );

  cameraServer.on(
    "/status",
    HTTP_GET,
    handleStatus
  );

  cameraServer.begin();

  Serial.println("CAMERA SERVER STARTED!");

  Serial.println("==========================");

}

// ==========================================
// MAIN LOOP
// ==========================================

void loop() {

  if (WiFi.status() != WL_CONNECTED) {

    static unsigned long reconnectTime = 0;

    if (millis() - reconnectTime > 5000) {

      reconnectTime = millis();

      Serial.println("RECONNECTING TO MASTER...");

      WiFi.reconnect();

    }

  }

  cameraServer.handleClient();

}
