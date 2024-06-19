
#include "esp_http_client.h"

#include <stdio.h>
#include <string.h>
#include "esp_log.h"
#include "nvs_flash.h"
#include <sys/param.h>
#include "esp_system.h"
#include "esp_tls.h"
#include "esp_crt_bundle.h"

#include "esp_https_ota.h"
#include "esp_wifi.h"
#include "esp_http_server.h"
#include "cJSON.h"
#include "freertos/event_groups.h"

#include "esp_task_wdt.h"

const char *TAG = "Client";
#define AP_SSID "esp_ap_ssid"
#define AP_PWD  "12341234"

#define MAX_HTTP_RECV_BUFFER 512
#define MAX_HTTP_OUTPUT_BUFFER 2048

EventGroupHandle_t xEvent;

void OTA_update_task (void *arg);
void Net_WIFI_mode_init (void);
esp_err_t _http_event_handler (esp_http_client_event_t * evt);
void event_handler (void *arg, esp_event_base_t event_base, int32_t event_id, void *event_data);
httpd_handle_t Prov_HttpServerInit (void);
esp_err_t Post_request (httpd_req_t * req);
esp_err_t Main_webpage (httpd_req_t * req);
void Request_GET_POST (void);

static char NEW_URL[128] = "";

#define OTA_BIT 1

void
app_main (void)
{
  // wifi 연결설정
  esp_err_t ret = nvs_flash_init ();
  if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND)
    {
      ESP_ERROR_CHECK (nvs_flash_erase ());
      ret = nvs_flash_init ();
    }
  ESP_ERROR_CHECK (ret);
  ESP_ERROR_CHECK (esp_netif_init ());
  ESP_ERROR_CHECK (esp_event_loop_create_default ());
  esp_netif_create_default_wifi_sta ();
  esp_netif_create_default_wifi_ap ();
  wifi_init_config_t wifi_init_cfg = WIFI_INIT_CONFIG_DEFAULT ();
  ESP_ERROR_CHECK (esp_wifi_init (&wifi_init_cfg));
  ESP_ERROR_CHECK (esp_event_handler_register (WIFI_EVENT, ESP_EVENT_ANY_ID, &event_handler, NULL));
  ESP_ERROR_CHECK (esp_event_handler_register (IP_EVENT, IP_EVENT_STA_GOT_IP, &event_handler, NULL));

  xEvent = xEventGroupCreate ();

  Net_WIFI_mode_init ();
  Prov_HttpServerInit ();

  xTaskCreate (OTA_update_task, "url ota", 1024 * 10, NULL, 10, NULL);

  vTaskDelay (pdMS_TO_TICKS (2000UL));
  // Request_GET_POST ();
}

esp_err_t                       // client
_http_event_handler (esp_http_client_event_t * evt)
{
  switch (evt->event_id)
    {
    case HTTP_EVENT_ERROR:
      ESP_LOGD (TAG, "HTTP_EVENT_ERROR");
      break;
    case HTTP_EVENT_ON_CONNECTED:
      ESP_LOGD (TAG, "HTTP_EVENT_ON_CONNECTED");
      break;
    case HTTP_EVENT_HEADER_SENT:
      ESP_LOGD (TAG, "HTTP_EVENT_HEADER_SENT");
      break;
    case HTTP_EVENT_ON_HEADER:
      ESP_LOGD (TAG, "HTTP_EVENT_ON_HEADER, key=%s, value=%s", evt->header_key, evt->header_value);
      break;
    case HTTP_EVENT_ON_DATA:
      ESP_LOGD (TAG, "HTTP_EVENT_ON_DATA, len=%d", evt->data_len);
      break;
    case HTTP_EVENT_ON_FINISH:
      ESP_LOGD (TAG, "HTTP_EVENT_ON_FINISH");
      break;
    case HTTP_EVENT_DISCONNECTED:
      ESP_LOGD (TAG, "HTTP_EVENT_DISCONNECTED");
      break;
    case HTTP_EVENT_REDIRECT:
      ESP_LOGD (TAG, "HTTP_EVENT_REDIRECT");
      break;
    }
  return ESP_OK;
}

void                            // wifi
event_handler (void *arg, esp_event_base_t event_base, int32_t event_id, void *event_data)
{
  if (event_base == WIFI_EVENT)
    {
      switch (event_id)
        {
        case WIFI_EVENT_STA_START:
          ESP_LOGI (TAG, "trying to conncet...");
          esp_wifi_connect ();
          break;
        case WIFI_EVENT_STA_DISCONNECTED:
          ESP_LOGI (TAG, "Disconnected. Retrying...");
          esp_wifi_connect ();
          break;
        case WIFI_EVENT_AP_STACONNECTED:
          ESP_LOGI (TAG, "SoftAP transport: Connected!");
          break;
        case WIFI_EVENT_AP_STADISCONNECTED:
          ESP_LOGI (TAG, "SoftAP transport: Disconnected!");
          break;
        }
    }
  else if (event_base == IP_EVENT)
    {
      switch (event_id)
        {
        case IP_EVENT_STA_GOT_IP:
          ip_event_got_ip_t * event = (ip_event_got_ip_t *) event_data;

          ESP_LOGI (TAG, "Connected with IP Address:" IPSTR, IP2STR (&event->ip_info.ip));
          break;
        }
    }
}

esp_err_t
Main_webpage (httpd_req_t * req)
{
  extern const unsigned char monitor_html_gz_start[] asm ("_binary_index_html_start");
  extern const unsigned char monitor_html_gz_end[] asm ("_binary_index_html_end");

  size_t monitor_html_gz_len = monitor_html_gz_end - monitor_html_gz_start;
  httpd_resp_set_type (req, "text/html");
  httpd_resp_set_hdr (req, "Content-Encoding", "html");

  ESP_LOGI (TAG, "HOME");

  return httpd_resp_send (req, (const char *) monitor_html_gz_start, monitor_html_gz_len);
}

esp_err_t
Post_request (httpd_req_t * req)
{
  char buf[256];

  httpd_req_recv (req, buf, sizeof (buf));
  cJSON *root = cJSON_Parse (buf);
  sprintf (NEW_URL, "%s", cJSON_GetObjectItem (root, "ota_url")->valuestring);
  ESP_LOGI (TAG, "url: %s", NEW_URL);

  // OTA_update_task ();
  xEventGroupSetBits (xEvent, OTA_BIT);
  httpd_resp_send (req, "wrong", sizeof ("wrong"));
  return ESP_OK;
}

httpd_handle_t
Prov_HttpServerInit (void)
{
  httpd_handle_t http_server = NULL;
  httpd_config_t http_config = HTTPD_DEFAULT_CONFIG ();

  // uri 설정
  httpd_uri_t web_main = {
    .uri = "/",
    .method = HTTP_GET,
    .handler = Main_webpage,
    .user_ctx = NULL
  };

  httpd_uri_t change_data = {
    .uri = "/change",
    .method = HTTP_POST,
    .handler = Post_request,
    .user_ctx = NULL
  };
  // 서버 시작
  if (httpd_start (&http_server, &http_config) == ESP_OK)
    {
      // 서버 시작 후 uri 등록
      httpd_register_uri_handler (http_server, &web_main);
      httpd_register_uri_handler (http_server, &change_data);

      return http_server;
    }

  return NULL;
}

void
Request_GET_POST (void)
{
  esp_http_client_config_t config = {
    .host = "192.168.4.1",
    .path = "/change",
    .method = HTTP_METHOD_POST,
    .transport_type = HTTP_TRANSPORT_OVER_TCP,
    .event_handler = _http_event_handler,
    .transport_type = ,
    .port = 80,
  };
  esp_http_client_handle_t client = esp_http_client_init (&config);
  esp_err_t err;
  // POST
  const char *post_data =
    "{\"ota_url\":\"https://drive.usercontent.google.com/u/0/uc?id=1FCxWEv1isOKGl17E88xSFQaHCANiMGcZ\"}";
  esp_http_client_set_header (client, "Content-Type", "application/json");
  esp_http_client_set_post_field (client, post_data, strlen (post_data));
  err = esp_http_client_perform (client);
  if (err == ESP_OK)
    {
      ESP_LOGI (TAG, "HTTP POST Status = %d, content_length = %" PRId64,
                esp_http_client_get_status_code (client), esp_http_client_get_content_length (client));
    }
  else
    {
      ESP_LOGE (TAG, "HTTP POST request failed: %s", esp_err_to_name (err));
    }

  // // GET
  // esp_http_client_set_url (client, "/");
  // esp_http_client_set_method (client, HTTP_METHOD_GET);
  // esp_http_client_set_header (client, "Content-Type", "html/text");
  // err = esp_http_client_perform (client);
  // if (err == ESP_OK)
  //   {
  //     ESP_LOGI (TAG, "HTTP GET Status = %d, content_length = %" PRId64,
  //               esp_http_client_get_status_code (client), esp_http_client_get_content_length (client));
  //   }
  // else
  //   {
  //     ESP_LOGE (TAG, "HTTP GET request failed: %s", esp_err_to_name (err));
  //   }


  esp_http_client_cleanup (client);
}

//https://drive.usercontent.google.com/u/0/uc?id=1FCxWEv1isOKGl17E88xSFQaHCANiMGcZ





/**
 * @brief url 설정/변경, 메서드 설정/변경하여 테스트
 * 
 * @note OTA 업데이트는 app_main에서 시도하지 말아야함.
 * 꼭 시도해야만 한다면 app_main task 사이즈 키울것. 
 * 실행하는 task의 크기가 작으면 ota 시도할 때 오류 발생
 */

const char *default_url = "old_update.url";
void
OTA_update_task (void *arg)
{
  // esp_http_client_handle_t client;

  while (1)
    {
      // vTaskDelay(pdMS_TO_TICKS(50UL));

      ESP_LOGI (TAG, "OTA");
      xEventGroupWaitBits (xEvent, OTA_BIT, pdTRUE, pdTRUE, portMAX_DELAY);
      ESP_LOGI (TAG, "Request Start");

      esp_http_client_config_t config = {
        .url = default_url,
        .event_handler = _http_event_handler,
        .crt_bundle_attach = esp_crt_bundle_attach,
        .keep_alive_enable = true,
        .port = 443,
      };

      // 아래 url로 HTTP GET요청 전송함
      if (strlen (NEW_URL) != 0)
        {
          const char *new_url = NEW_URL;
          config.url = new_url;
        }

      esp_https_ota_config_t ota_config = {
        .http_config = &config,
        // .partial_http_download
      };

      ESP_LOGI (TAG, "Attempting to download update from %s", config.url);
      esp_err_t ret = esp_https_ota (&ota_config);
      if (ret == ESP_OK)
        {
          ESP_LOGI (TAG, "OTA Succeed, Rebooting...");
          esp_restart ();
        }
      else
        {
          ESP_LOGE (TAG, "Firmware upgrade failed");
        }

    }
}

void
Net_WIFI_mode_init (void)
{
  // ap 설정
  wifi_config_t ap_wifi_config = {
    .ap = {
           .ssid = AP_SSID,
           .ssid_len = strlen (AP_SSID),
           .password = AP_PWD,
           .channel = 1,
           .max_connection = 4,
           .authmode = WIFI_AUTH_WPA2_PSK,
           .pmf_cfg = {
                       .required = true,
                       }
           }
  };
  wifi_config_t sta_wifi_config = { };
  sprintf ((char *) sta_wifi_config.sta.ssid, "HEDY_2.4G");
  sprintf ((char *) sta_wifi_config.sta.password, "hedy0806");
  esp_wifi_set_mode (WIFI_MODE_APSTA);
  esp_wifi_set_config (ESP_IF_WIFI_AP, &ap_wifi_config);  // url 입력받음
  esp_wifi_set_config (ESP_IF_WIFI_STA, &sta_wifi_config);  // http client
  esp_wifi_start ();
}
