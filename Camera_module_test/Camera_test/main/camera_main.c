
#include "esp_camera.h"

#include "esp_log.h"
#include "esp_http_server.h"
#include "app_wifi.h"

static const char *TAG = "UNIT_TEST";

// Prototype BEGIN
static esp_err_t capture_handler (httpd_req_t * req);
static esp_err_t stream_handler (httpd_req_t * req);
void open_httpd ();
static void task_process_handler (void *arg);
void camera_settings (const pixformat_t pixel_fromat, const framesize_t frame_size);
void xclk_handler_task (void *arg);

// Prototype END

// Camera Config BEGIN
/**
 * who_camera.h?�� 보드�?? ?��?�� ?��?��.
*/
#include "who_camera.h"

#define CAMERA_MODULE_NAME "ESP-S3-EYE"
#define CAMERA_PIN_PWDN -1
#define CAMERA_PIN_RESET -1

#define CAMERA_PIN_VSYNC 6
#define CAMERA_PIN_HREF 7
#define CAMERA_PIN_PCLK 13
#define CAMERA_PIN_XCLK 15

#define CAMERA_PIN_SIOD 4
#define CAMERA_PIN_SIOC 5

#define CAMERA_PIN_D0 11
#define CAMERA_PIN_D1 9
#define CAMERA_PIN_D2 8
#define CAMERA_PIN_D3 10
#define CAMERA_PIN_D4 12
#define CAMERA_PIN_D5 18
#define CAMERA_PIN_D6 17
#define CAMERA_PIN_D7 16

#define XCLK_FREQ_HZ 10000000
// Camera Config END

/**
 * - PART_BOUNDARY
 *  HTTP ?��로토콜의 바디 �??분에 ?��?��?���?? ?��?�� �??분으�?? ?��?��?�� 보내?��
 * �???�� ?��?�� ?��?��림에?�� 경계�?? ?��????��.
 * 
 * - _STREAM_CONTENT_TYPE
 * - _STREAM_BOUNDARY
 * HTTP ?��?��?��?�� type�?? PART_BOUNDARY ?��?��
 * 
 * - _STREAM_PART
 * Timestamp??? content-type?�� image/jpeg�?? ?��?��
 * 
*/
#define PART_BOUNDARY "123456789000000000000987654321"
static const char *_STREAM_CONTENT_TYPE = "multipart/x-mixed-replace;boundary=" PART_BOUNDARY;
static const char *_STREAM_BOUNDARY = "\r\n--" PART_BOUNDARY "\r\n";
static const char *_STREAM_PART = "Content-Type: image/jpeg\r\nContent-Length: %u\r\nX-Timestamp: %d.%06d\r\n\r\n";

httpd_handle_t stream_httpd = NULL;
httpd_handle_t camera_httpd = NULL;
static QueueHandle_t xQueueCameraFrame = NULL;

void
app_main (void)
{
  app_wifi_main ();

  xQueueCameraFrame = xQueueCreate (5, sizeof (camera_fb_t *));
  camera_settings (PIXFORMAT_JPEG, FRAMESIZE_QVGA);
  //camera_settings (PIXFORMAT_RGB565,FRAMESIZE_QVGA); // format �??�??
  //camera_settings (PIXFORMAT_JPEG, FRAMESIZE_XGA); // frame size �??�??
  open_httpd ();
  xTaskCreate (xclk_handler_task, "xclk task", 1024 * 10, NULL, 5, NULL);
}

static esp_err_t
capture_handler (httpd_req_t * req)
{
  camera_fb_t *frame = NULL;
  esp_err_t res = ESP_OK;

  if (xQueueReceive (xQueueCameraFrame, &frame, portMAX_DELAY))
    {
      httpd_resp_set_type (req, "image/jpeg");
      httpd_resp_set_hdr (req, "Content-Disposition", "inline; filename=capture.jpg");
      httpd_resp_set_hdr (req, "Access-Control-Allow-Origin", "*");

      res = httpd_resp_send (req, (const char *) frame->buf, frame->len);
      esp_camera_fb_return (frame);
    }
  else
    {
      ESP_LOGE (TAG, "Camera capture failed");
      httpd_resp_send_500 (req);
      return ESP_FAIL;
    }

  return res;
}

static esp_err_t
stream_handler (httpd_req_t * req)
{
  camera_fb_t *frame = NULL;
  struct timeval _timestamp;
  esp_err_t res = ESP_OK;
  size_t _jpg_buf_len = 0;
  uint8_t *_jpg_buf = NULL;
  char *part_buf[128];

  res = httpd_resp_set_type (req, _STREAM_CONTENT_TYPE);
  if (res != ESP_OK)
    {
      return res;
    }

  httpd_resp_set_hdr (req, "Access-Control-Allow-Origin", "*");
  httpd_resp_set_hdr (req, "X-Framerate", "60");

  while (true)
    {
      if (xQueueReceive (xQueueCameraFrame, &frame, portMAX_DELAY))
        {
          _timestamp.tv_sec = frame->timestamp.tv_sec;
          _timestamp.tv_usec = frame->timestamp.tv_usec;

          _jpg_buf = frame->buf;
          _jpg_buf_len = frame->len;
        }
      else
        {
          res = ESP_FAIL;
        }

      if (res == ESP_OK)
        {
          res = httpd_resp_send_chunk (req, _STREAM_BOUNDARY, strlen (_STREAM_BOUNDARY));
        }

      if (res == ESP_OK)
        {
          size_t hlen =
            snprintf ((char *) part_buf, 128, _STREAM_PART, _jpg_buf_len, _timestamp.tv_sec, _timestamp.tv_usec);
          res = httpd_resp_send_chunk (req, (const char *) part_buf, hlen);
        }

      if (res == ESP_OK)
        {
          res = httpd_resp_send_chunk (req, (const char *) _jpg_buf, _jpg_buf_len);
        }

      esp_camera_fb_return (frame);

      if (res != ESP_OK)
        {
          break;
        }
    }

  return res;
}

void
open_httpd ()
{
  httpd_config_t config = HTTPD_DEFAULT_CONFIG ();

  httpd_uri_t stream_uri = {
    .uri = "/stream",
    .method = HTTP_GET,
    .handler = stream_handler,
    .user_ctx = NULL
  };

  httpd_uri_t capture_uri = {
    .uri = "/capture",
    .method = HTTP_GET,
    .handler = capture_handler,
    .user_ctx = NULL
  };

  ESP_LOGI (TAG, "Starting web server on port: '%d'", config.server_port);
  if (httpd_start (&camera_httpd, &config) == ESP_OK)
    {
      //httpd_register_uri_handler (camera_httpd, &stream_uri);
      httpd_register_uri_handler (camera_httpd, &capture_uri);
    }

  // ?��?��?�� ?��?��리밍 + 캡쳐�?? ?��?��?�� ?��고싶?���?? ?��버�?? ?���?? ?��?��?�� ?��.
  config.server_port += 1;
  config.ctrl_port += 1;
  ESP_LOGI (TAG, "Starting stream server on port: '%d'", config.server_port);
  if (httpd_start (&stream_httpd, &config) == ESP_OK)
    {
      httpd_register_uri_handler (stream_httpd, &stream_uri);
    }
}

static void
task_process_handler (void *arg)
{
  while (true)
    {
      camera_fb_t *frame = esp_camera_fb_get ();
      if (frame)
        xQueueSend (xQueueCameraFrame, &frame, portMAX_DELAY);
    }
}

void
camera_settings (const pixformat_t pixel_fromat, const framesize_t frame_size)
{
  ESP_LOGI (TAG, "Camera module is %s", CAMERA_MODULE_NAME);

  camera_config_t config;
  config.ledc_channel = LEDC_CHANNEL_0;
  config.ledc_timer = LEDC_TIMER_0;
  config.pin_d0 = CAMERA_PIN_D0;
  config.pin_d1 = CAMERA_PIN_D1;
  config.pin_d2 = CAMERA_PIN_D2;
  config.pin_d3 = CAMERA_PIN_D3;
  config.pin_d4 = CAMERA_PIN_D4;
  config.pin_d5 = CAMERA_PIN_D5;
  config.pin_d6 = CAMERA_PIN_D6;
  config.pin_d7 = CAMERA_PIN_D7;
  config.pin_xclk = CAMERA_PIN_XCLK;
  config.pin_pclk = CAMERA_PIN_PCLK;
  config.pin_vsync = CAMERA_PIN_VSYNC;
  config.pin_href = CAMERA_PIN_HREF;
  config.pin_sscb_sda = CAMERA_PIN_SIOD;
  config.pin_sscb_scl = CAMERA_PIN_SIOC;
  config.pin_pwdn = CAMERA_PIN_PWDN;
  config.pin_reset = CAMERA_PIN_RESET;
  config.xclk_freq_hz = XCLK_FREQ_HZ;
  //config.xclk_freq_hz = 400000;  
  config.pixel_format = pixel_fromat;
  config.frame_size = frame_size;
  config.jpeg_quality = 12;
  config.fb_count = 5;
  config.fb_location = CAMERA_FB_IN_PSRAM;
  config.grab_mode = CAMERA_GRAB_WHEN_EMPTY;
  //config.grab_mode = CAMERA_GRAB_LATEST;

  esp_err_t err = esp_camera_init (&config);
  if (err != ESP_OK)
    {
      ESP_LOGE (TAG, "Camera init fail");
      return;
    }

  sensor_t *s = esp_camera_sensor_get ();

  // ?��?�� 반전
  if (s->id.PID == OV2640_PID)
    {
      s->set_vflip (s, 1);
    }

  xTaskCreatePinnedToCore (task_process_handler, TAG, 3 * 1024, NULL, 5, NULL, 1);
}


void
xclk_handler_task (void *arg)
{
  int xclk = 1;
  while (1)
    {
      ESP_LOGI (TAG, "Set XCLK: %d MHz", xclk);

      sensor_t *s = esp_camera_sensor_get ();
        int res = s->set_xclk (s, LEDC_TIMER_0, xclk);
      vTaskDelay (pdMS_TO_TICKS (5000UL));
      xclk += 4;
      if (xclk > 20)
        {
          xclk = 1;
        }
    }
}
