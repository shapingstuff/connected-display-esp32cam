/*
  Edited code from Rui Santos
  Complete project details at:
  https://RandomNerdTutorials.com/esp32-cam-http-post-php-arduino/
  https://RandomNerdTutorials.com/esp32-cam-post-image-photo-server/

  Permission is hereby granted, free of charge, to any person obtaining a copy
  of this software and associated documentation files.

  The above copyright notice and this permission notice shall be included in all
  copies or substantial portions of the Software.
*/

#include <Arduino.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include "soc/soc.h"
#include "soc/rtc_cntl_reg.h"
#include "esp_camera.h"
#include <WiFiManager.h>
#include <Preferences.h>

Preferences prefs;

String namePrefix = "";
String displayName = "unknown";
String serverName = "http://irs-iot.ddns.net"; //
String serverPath = "/upload";                 // The default serverPath should be upload.php
const int serverPort = 80;                     // server port for HTTPS

bool shouldSaveConfig = false;

WiFiClientSecure client;

// CAMERA_MODEL_AI_THINKER
#define PWDN_GPIO_NUM 32
#define RESET_GPIO_NUM -1
#define XCLK_GPIO_NUM 0
#define SIOD_GPIO_NUM 26
#define SIOC_GPIO_NUM 27

#define Y9_GPIO_NUM 35
#define Y8_GPIO_NUM 34
#define Y7_GPIO_NUM 39
#define Y6_GPIO_NUM 36
#define Y5_GPIO_NUM 21
#define Y4_GPIO_NUM 19
#define Y3_GPIO_NUM 18
#define Y2_GPIO_NUM 5
#define VSYNC_GPIO_NUM 25
#define HREF_GPIO_NUM 23
#define PCLK_GPIO_NUM 22

#define FLASH_PIN 4
#define BUILTIN_LED 33

String timerIntervalField = "30"; // time between each HTTP POST image
unsigned int timerInterval = 30000;
unsigned long previousMillis = 0; // last time image was sent

void saveConfigCallback()
{
  Serial.println("WiFiManager settings changed");
  shouldSaveConfig = true;
}

void setup()
{
  WRITE_PERI_REG(RTC_CNTL_BROWN_OUT_REG, 0);
  Serial.begin(115200);

  pinMode(FLASH_PIN, OUTPUT);
  pinMode(BUILTIN_LED, OUTPUT);

  Serial.print("ESP32-CAM");

  WiFiManager wm;

  WiFiManagerParameter customDisplayName("name", "Name", displayName.c_str(), 40);
  WiFiManagerParameter customTimerIntervalSeconds("interval", "Interval (seconds)", timerIntervalField.c_str(), 4);
  wm.setSaveConfigCallback(saveConfigCallback);
  wm.addParameter(&customDisplayName);
  wm.addParameter(&customTimerIntervalSeconds);

  bool res;
  res = wm.autoConnect(); // password protected ap

  if (!res)
  {
    Serial.println("Failed to connect to WiFi.");
  }
  else
  {
    Serial.println("Connected to WiFi.");
  }

  // load / save camera name
  prefs.begin("conndisp");
  if (shouldSaveConfig)
  {
    displayName = String(customDisplayName.getValue());
    prefs.putString("name", displayName);
    timerIntervalField = String(customTimerIntervalSeconds.getValue());
    timerInterval = timerIntervalField.toInt() * 1000;
    prefs.putInt("interval", timerInterval);
  }
  else
  {
    displayName = prefs.getString("name", "unknown");
    timerInterval = prefs.getInt("interval", 30000);
  }
  Serial.print("Display name: ");
  Serial.println(namePrefix + displayName);
  Serial.print("Timer interval: ");
  Serial.println(timerInterval);
  prefs.end();

  Serial.println();
  Serial.print("ESP32-CAM IP Address: ");
  Serial.println(WiFi.localIP());

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
  config.pin_sscb_sda = SIOD_GPIO_NUM;
  config.pin_sscb_scl = SIOC_GPIO_NUM;
  config.pin_pwdn = PWDN_GPIO_NUM;
  config.pin_reset = RESET_GPIO_NUM;
  config.xclk_freq_hz = 20000000;
  config.pixel_format = PIXFORMAT_JPEG;

  // init with high specs to pre-allocate larger buffers
  if (psramFound())
  {
    Serial.println("PSRAM found!");
    // config.frame_size = FRAMESIZE_SVGA;
    config.frame_size = FRAMESIZE_UXGA;
    config.jpeg_quality = 6; // 0-63 lower number means higher quality
    config.fb_count = 2;
  }
  else
  {
    config.frame_size = FRAMESIZE_CIF;
    config.jpeg_quality = 12; // 0-63 lower number means higher quality
    config.fb_count = 1;
  }

  // camera init
  esp_err_t err = esp_camera_init(&config);
  if (err != ESP_OK)
  {
    Serial.printf("Camera init failed with error 0x%x", err);
    delay(1000);
    ESP.restart();
  }

  /*
   s->set_res_raw(s, resolution, unused, unused, unused, offset_x, offset_y, total_x, total_y, width, height, unused, unused);
  resolution = 0 \\ 1600 x 1200
  resolution = 1 \\  800 x  600
  resolution = 2 \\  400 x  296
*/
  // set the ideal crop size for the current shelf dimensions
  sensor_t *s = esp_camera_sensor_get();
  s->set_res_raw(s, 0, 0, 0, 0, 0, 90, 1600, 1000, 1600, 1000, true, true);

  sendPhoto();
}

void loop()
{
  unsigned long currentMillis = millis();
  if (currentMillis - previousMillis >= timerInterval)
  {
    sendPhoto();
    previousMillis = currentMillis;
  }
}

String sendPhoto()
{
  String getAll;
  String getBody;

  camera_fb_t *fb = NULL;
  fb = esp_camera_fb_get();
  if (!fb)
  {
    Serial.println("Camera capture failed");
    delay(1000);
    ESP.restart();
  }

  Serial.println("Connecting to server: " + serverName);

  client.setInsecure(); // skip certificate validation
  if (client.connect(serverName.c_str(), serverPort))
  {
    Serial.println("Connection successful!");
    String head = "--RandomNerdTutorials\r\nContent-Disposition: form-data; name=\"image\"; filename=\"" + namePrefix + displayName + ".jpg\"\r\nContent-Type: image/jpeg\r\n\r\n";
    String tail = "\r\n--RandomNerdTutorials--\r\n";

    uint32_t imageLen = fb->len;
    uint32_t extraLen = head.length() + tail.length();
    uint32_t totalLen = imageLen + extraLen;

    client.println("POST " + serverPath + " HTTP/1.1");
    client.println("Host: " + serverName);
    client.println("Content-Length: " + String(totalLen));
    client.println("Content-Type: multipart/form-data; boundary=RandomNerdTutorials");
    client.println();
    client.print(head);

    uint8_t *fbBuf = fb->buf;
    size_t fbLen = fb->len;

    for (size_t n = 0; n < fbLen; n = n + 1024)
    {
      if (n + 1024 < fbLen)
      {
        client.write(fbBuf, 1024);
        fbBuf += 1024;
      }
      else if (fbLen % 1024 > 0)
      {
        size_t remainder = fbLen % 1024;
        client.write(fbBuf, remainder);
      }
    }
    client.print(tail);

    esp_camera_fb_return(fb);

    int timoutTimer = 10000;
    long startTimer = millis();
    boolean state = false;

    while ((startTimer + timoutTimer) > millis())
    {
      Serial.print(".");
      delay(100);
      while (client.available())
      {
        char c = client.read();
        if (c == '\n')
        {
          if (getAll.length() == 0)
          {
            state = true;
          }
          getAll = "";
        }
        else if (c != '\r')
        {
          getAll += String(c);
        }
        if (state == true)
        {
          getBody += String(c);
        }
        startTimer = millis();
      }
      if (getBody.length() > 0)
      {
        break;
      }
    }
    Serial.println();
    client.stop();
    Serial.println(getBody);
  }
  else
  {
    getBody = "Connection to " + serverName + " failed.";
    Serial.println(getBody);
  }
  digitalWrite(BUILTIN_LED, HIGH);
  delay(100);
  digitalWrite(BUILTIN_LED, LOW);
  return getBody;
}