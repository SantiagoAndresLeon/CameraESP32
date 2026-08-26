#include "esp_camera.h"
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include "mbedtls/base64.h"

// ===========================
// Modelo de camara en board_config.h
// ===========================
#include "board_config.h"

// ===========================
// Credenciales WiFi
// ===========================
const char *ssid = "WiFi-Repeater";
const char *password = "privada2026";

// ===========================
// URL del Web App de Google Apps Script
// >>> REEMPLAZA por la tuya (termina en /exec) <<<
// ===========================
const char *scriptURL = "https://script.google.com/macros/s/AKfycbxotjjy7gyhs1xz4QtcwMHmvpQglSJk772lVJgRIqsDnm_QpXyRr32yYE4g7IRdrx0edw/exec";

// Intervalo entre fotos (60 minutos)
const unsigned long INTERVALO_MS = 60UL * 60UL * 1000UL;
unsigned long ultimaFoto = 0;
bool primeraFoto = true;

void takeAndUploadPhoto();

void setup() {
  Serial.begin(115200);
  Serial.setDebugOutput(true);
  Serial.println();

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
  config.xclk_freq_hz = 20000000;
  config.frame_size = FRAMESIZE_QSXGA;  // 2592x1944 (5 MP, maximo del OV5640)
  config.pixel_format = PIXFORMAT_JPEG;
  config.grab_mode = CAMERA_GRAB_WHEN_EMPTY;
  config.fb_location = CAMERA_FB_IN_PSRAM;
  config.jpeg_quality = 12;
  config.fb_count = 1;

  if (psramFound()) {
    config.jpeg_quality = 12;  // a 5 MP conviene un poco mas de compresion
    config.fb_count = 1;       // un solo buffer: 5 MP ocupa mucho en PSRAM
    config.grab_mode = CAMERA_GRAB_LATEST;
  } else {
    config.frame_size = FRAMESIZE_SVGA;
    config.fb_location = CAMERA_FB_IN_DRAM;
  }

  esp_err_t err = esp_camera_init(&config);
  if (err != ESP_OK) {
    Serial.printf("Camera init failed with error 0x%x", err);
    return;
  }

  sensor_t *s = esp_camera_sensor_get();
  // Ajustes de imagen (el sensor se detecta automaticamente: OV3660 u OV5640)
  // Rango de cada ajuste: -2 a 2
  if (s->id.PID == OV3660_PID || s->id.PID == OV5640_PID) {
    s->set_brightness(s, -2);   // brillo neutro
    s->set_contrast(s, 1);     // un poco mas de contraste
    s->set_saturation(s, 2);   // mas saturacion (mas color)
  }
  s->set_lenc(s, 1);           // correccion de lente: reduce el oscurecimiento (vineta) en los bordes
  // Si la imagen sale al reves o en espejo, descomenta lo que necesites:
     s->set_vflip(s, 1);    // voltear vertical
  // s->set_hmirror(s, 1);  // voltear horizontal

  // Resolucion de las fotos que se suben.
  // QSXGA = 2592x1944 (5 MP), el maximo del OV5640.
  s->set_framesize(s, FRAMESIZE_QSXGA);

  WiFi.begin(ssid, password);
  WiFi.setSleep(false);
  Serial.print("WiFi connecting");
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("\nWiFi connected");
  Serial.print("IP: ");
  Serial.println(WiFi.localIP());
}

void loop() {
  if (primeraFoto || millis() - ultimaFoto >= INTERVALO_MS) {
    primeraFoto = false;
    ultimaFoto = millis();
    takeAndUploadPhoto();
  }
  delay(1000);
}

void takeAndUploadPhoto() {
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("Sin WiFi, reintentando...");
    WiFi.reconnect();
    return;
  }

  camera_fb_t *fb = esp_camera_fb_get();
  if (!fb) {
    Serial.println("Error al capturar la foto");
    return;
  }
  Serial.printf("Foto capturada: %u bytes\n", fb->len);

  // Calcular tamano necesario y codificar en base64
  size_t bufLen = 0;
  mbedtls_base64_encode(NULL, 0, &bufLen, fb->buf, fb->len);
  uint8_t *encoded = (uint8_t *) ps_malloc(bufLen);
  if (!encoded) {
    Serial.println("Sin memoria para base64");
    esp_camera_fb_return(fb);
    return;
  }
  size_t realLen = 0;
  mbedtls_base64_encode(encoded, bufLen, &realLen, fb->buf, fb->len);
  esp_camera_fb_return(fb);  // liberar el buffer de la camara enseguida

  WiFiClientSecure client;
  client.setInsecure();  // no valida certificado (suficiente para este caso)
  HTTPClient http;
  http.begin(client, scriptURL);
  http.setFollowRedirects(HTTPC_FORCE_FOLLOW_REDIRECTS);  // Apps Script redirige (302)
  http.addHeader("Content-Type", "text/plain");

  Serial.println("Subiendo a Google Drive...");
  int httpCode = http.POST(encoded, realLen);
  if (httpCode > 0) {
    Serial.printf("Respuesta HTTP: %d\n", httpCode);
    Serial.println(http.getString());
  } else {
    Serial.printf("Error en POST: %s\n", http.errorToString(httpCode).c_str());
  }
  http.end();
  free(encoded);
}
