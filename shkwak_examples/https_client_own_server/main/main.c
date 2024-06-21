#include "esp_http_client.h"

#include <stdio.h>
#include <string.h>
#include "esp_log.h"
#include "nvs_flash.h"
#include <sys/param.h>
#include "esp_system.h"
#include "esp_tls.h"
#include "esp_crt_bundle.h"
#include "esp_wifi.h"
#include "esp_https_server.h"
#include "esp_tls.h"
#include "cJSON.h"

const char *TAG = "client";
const char *cc_ssid = "HEDY_2.4G";
const char *cc_pwd = "hedy0806";

extern const unsigned char root_cert_pem_start[] asm ("_binary_servercert_pem_start");
extern const unsigned char root_cert_pem_end[] asm ("_binary_servercert_pem_end");

extern const unsigned char root_key_pem_start[] asm ("_binary_prvtkey_pem_start");
extern const unsigned char root_key_pem_end[] asm ("_binary_prvtkey_pem_end");
/**
 * //TODO
 * 1. 인증서 입력
 * 2. api 받아오기
 * 3. get / post / put 등 여러 요청 사용
 * 4. 각 요청별 별개의 함수?
 */
void Net_WIFI_mode_init (void);
void event_handler (void *arg, esp_event_base_t event_base, int32_t event_id, void *event_data);
void Request_GET_POST (void);
void http_task (void *arg);
httpd_handle_t Prov_HttpServerInit (void);



void
app_main (void)
{
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

  Net_WIFI_mode_init ();
  Prov_HttpServerInit ();

  vTaskDelay (pdMS_TO_TICKS (1000UL));

  xTaskCreate (http_task, "task 1", 1024 * 8, NULL, 5, NULL);
}

void
http_task (void *arg)
{
  Request_GET_POST ();
  vTaskDelete (NULL);
}

void
Net_WIFI_mode_init (void)
{
  wifi_config_t ap_wifi_config = {
    .ap.ssid = "ESP_https_server",
    .ap.password = "qwer1234",
    .ap.ssid_len = strlen ("ESP_https_server"),
    .ap.channel = 1,
    .ap.max_connection = 4,
    .ap.authmode = WIFI_AUTH_WPA2_PSK,

    .ap.pmf_cfg.required = true,
  };
  wifi_config_t sta_wifi_config = { };
  sprintf ((char *) sta_wifi_config.sta.ssid, "HEDY_2.4G");
  sprintf ((char *) sta_wifi_config.sta.password, "hedy0806");
  esp_wifi_set_mode (WIFI_MODE_APSTA);
  esp_wifi_set_config (ESP_IF_WIFI_AP, &ap_wifi_config);  // https server
  esp_wifi_set_config (ESP_IF_WIFI_STA, &sta_wifi_config);  // http client
  esp_wifi_start ();
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
      printf ("%s", (char *) evt->data);
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


void
Request_GET_POST (void)
{
  esp_http_client_config_t config = {
    .url = "https://192.168.4.1/change",
    .transport_type = HTTP_TRANSPORT_OVER_TCP,
    .event_handler = _http_event_handler,
    .port = 443,
    .method = HTTP_METHOD_POST,
    .cert_pem = (const char *)root_cert_pem_start,
  };
  esp_http_client_handle_t client = esp_http_client_init (&config);
  esp_err_t err;

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

  // POST
  const char *post_data =
    "{\"ota_url\":\"hello world\"}";
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
  esp_http_client_cleanup (client);
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
  cJSON *temp = cJSON_GetObjectItem (root, "ota_url");
  ESP_LOGI (TAG, "url: %s", temp->valuestring);

  httpd_resp_send (req, "wrong", sizeof ("wrong"));
  cJSON_Delete (root);
  cJSON_Delete (temp);
  return ESP_OK;
}


httpd_handle_t
Prov_HttpServerInit (void)
{
  httpd_handle_t http_server = NULL;
  httpd_ssl_config_t http_config = HTTPD_SSL_CONFIG_DEFAULT ();
  http_config.servercert = root_cert_pem_start;
  http_config.servercert_len = root_cert_pem_end - root_cert_pem_start;
  http_config.prvtkey_pem = root_key_pem_start;
  http_config.prvtkey_len = root_key_pem_end - root_key_pem_start;
  http_config.httpd.server_port = 400;

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
  if (httpd_ssl_start (&http_server, &http_config) == ESP_OK)
    {
      // 서버 시작 후 uri 등록
      httpd_register_uri_handler (http_server, &web_main);
      httpd_register_uri_handler (http_server, &change_data);

      return http_server;
    }

  return NULL;
}
