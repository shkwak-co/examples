/* Simple HTTP + SSL Server Example

   This example code is in the Public Domain (or CC0 licensed, at your option.)

   Unless required by applicable law or agreed to in writing, this
   software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
   CONDITIONS OF ANY KIND, either express or implied.
*/

#include "esp_http_client.h"

#include <esp_wifi.h>
#include <esp_event.h>
#include <esp_log.h>
#include <esp_system.h>
#include <nvs_flash.h>
#include <sys/param.h>
#include "esp_netif.h"

#include <esp_https_server.h>
#include "esp_tls.h"
#include "sdkconfig.h"
// #include "cJSON.h"

/* A simple example that demonstrates how to create GET and POST
 * handlers and start an HTTPS server.
*/

// https server
void event_handler (void *arg, esp_event_base_t event_base, int32_t event_id, void *event_data);
void Net_WIFI_mode_init (void);

// https client
esp_err_t _http_event_handler (esp_http_client_event_t * evt);
void Request_GET (void);
void Request_POST (void);
void Request_PATCH (void);
static void http_rest_with_hostname_path (void);


static const char *TAG = "example";
// extern const char cert_start[] asm ("_binary_servercert_pem_start");
// extern const char cert_end[] asm ("_binary_servercert_pem_end");

// extern const char naver_cert_start[] asm ("_binary_naver_root_crt_start");
// extern const char naver_cert_end[] asm ("_binary_naver_root_crt_end");

extern const char root_cert_start[] asm ("_binary_root_crt_start");
extern const char root_cert_end[] asm ("_binary_root_crt_end");

void
app_main (void)
{
  static httpd_handle_t server = NULL;
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
  /* Register event handlers to start server when Wi-Fi or Ethernet is connected,
   * and stop server when disconnection happens.
   */
  wifi_init_config_t wifi_init_cfg = WIFI_INIT_CONFIG_DEFAULT ();
  ESP_ERROR_CHECK (esp_wifi_init (&wifi_init_cfg));

  ESP_ERROR_CHECK (esp_event_handler_register (WIFI_EVENT, ESP_EVENT_ANY_ID, &event_handler, &server));


  Net_WIFI_mode_init ();


  vTaskDelay (pdMS_TO_TICKS (10000UL));
  // Request_GET ();
  // vTaskDelay (pdMS_TO_TICKS (5000UL));
  // Request_POST ();
  // vTaskDelay (pdMS_TO_TICKS (5000UL));
  // Request_PATCH ();
  http_rest_with_hostname_path ();

}


esp_err_t
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
      // printf ("%s\r\n", (char *) evt->data);
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
Request_GET (void)
{
  esp_http_client_config_t config = {
    .host = "jsonplaceholder.typicode.com",
    .path = "/users/10",
    .event_handler = _http_event_handler,
    // .cert_pem = cert_start,
    .transport_type = HTTP_TRANSPORT_OVER_SSL,
    .cert_pem = root_cert_start,
  };
  esp_http_client_handle_t client = esp_http_client_init (&config);
  esp_err_t err;
  // GET 
  err = esp_http_client_perform (client);
  if (err == ESP_OK)
    {
      ESP_LOGI (TAG, "HTTP GET Status = %d, content_length = %" PRId64,
                esp_http_client_get_status_code (client), esp_http_client_get_content_length (client));
    }
  else
    {
      ESP_LOGE (TAG, "HTTP GET request failed: %s", esp_err_to_name (err));
    }

  esp_http_client_cleanup (client);
}


void
Request_POST (void)
{
  esp_http_client_config_t config = {
    .host = "jsonplaceholder.typicode.com",
    .path = "/users",
    .event_handler = _http_event_handler,
    .method = HTTP_METHOD_POST,
    // .cert_pem = cert_start,
    .transport_type = HTTP_TRANSPORT_OVER_SSL,
    .cert_pem = root_cert_start,
  };
  esp_http_client_handle_t client = esp_http_client_init (&config);
  esp_err_t err;

  // POST
  const char *post_data = "{\"new\":\"11\",\"name_no\":\"sh\",\"username\":\"kkkkk\"}";
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


void
Request_PATCH (void)
{
  esp_http_client_config_t config = {
    .host = "jsonplaceholder.typicode.com",
    .path = "/users/10",
    .event_handler = _http_event_handler,
    .method = HTTP_METHOD_PATCH,
    // .cert_pem = cert_start,
    .cert_pem = root_cert_start,
  };
  esp_http_client_handle_t client = esp_http_client_init (&config);
  esp_err_t err;

  // POST
  const char *post_data = "{\"id\":\"11\",\"name\":\"sh\",\"username\":\"kkkkk\", \"bs\":\"yy\"}";
  esp_http_client_set_header (client, "Content-Type", "application/json");
  esp_http_client_set_post_field (client, post_data, strlen (post_data));
  err = esp_http_client_perform (client);
  if (err == ESP_OK)
    {
      ESP_LOGI (TAG, "HTTP PATCH Status = %d, content_length = %" PRId64,
                esp_http_client_get_status_code (client), esp_http_client_get_content_length (client));
    }
  else
    {
      ESP_LOGE (TAG, "HTTP PATCH request failed: %s", esp_err_to_name (err));
    }
  esp_http_client_cleanup (client);
}



static void
http_rest_with_hostname_path (void)
{
  esp_http_client_config_t config = {
    .host = "jsonplaceholder.typicode.com",
    .path = "/users/10",
    .transport_type = HTTP_TRANSPORT_OVER_TCP,
    .event_handler = _http_event_handler,
  };
  esp_http_client_handle_t client = esp_http_client_init (&config);

  // GET
  esp_err_t err = esp_http_client_perform (client);
  if (err == ESP_OK)
    {
      ESP_LOGI (TAG, "HTTP GET Status = %d, content_length = %" PRId64,
                esp_http_client_get_status_code (client), esp_http_client_get_content_length (client));
    }
  else
    {
      ESP_LOGE (TAG, "HTTP GET request failed: %s", esp_err_to_name (err));
    }

  vTaskDelay (pdMS_TO_TICKS (5000UL));

  //POST
  esp_http_client_set_url (client, "/users");
  const char *post_data = "field1=value1&field2=value2";
  esp_http_client_set_timeout_ms (client, 4000);
  esp_http_client_set_method (client, HTTP_METHOD_POST);
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

  vTaskDelay (pdMS_TO_TICKS (5000UL));
  //PUT

  // esp_http_client_set_method (client, HTTP_METHOD_PUT);
  // err = esp_http_client_perform (client);
  // if (err == ESP_OK)
  //   {
  //     ESP_LOGI (TAG, "HTTP PUT Status = %d, content_length = %" PRId64,
  //               esp_http_client_get_status_code (client), esp_http_client_get_content_length (client));
  //   }
  // else
  //   {
  //     ESP_LOGE (TAG, "HTTP PUT request failed: %s", esp_err_to_name (err));
  //   }

  // vTaskDelay (pdMS_TO_TICKS (5000UL));

  //PATCH

  esp_http_client_set_url (client, "/users/10");
  const char *patch_data = "website=naver.com";
  esp_http_client_set_method (client, HTTP_METHOD_PATCH);
  esp_http_client_set_post_field (client, patch_data, strlen (patch_data));
  err = esp_http_client_perform (client);
  if (err == ESP_OK)
    {
      ESP_LOGI (TAG, "HTTP PATCH Status = %d, content_length = %" PRId64,
                esp_http_client_get_status_code (client), esp_http_client_get_content_length (client));
    }
  else
    {
      ESP_LOGE (TAG, "HTTP PATCH request failed: %s", esp_err_to_name (err));
    }

  vTaskDelay (pdMS_TO_TICKS (5000UL));

  //DELETE

  esp_http_client_set_method (client, HTTP_METHOD_DELETE);
  err = esp_http_client_perform (client);
  if (err == ESP_OK)
    {
      ESP_LOGI (TAG, "HTTP DELETE Status = %d, content_length = %" PRId64,
                esp_http_client_get_status_code (client), esp_http_client_get_content_length (client));
    }
  else
    {
      ESP_LOGE (TAG, "HTTP DELETE request failed: %s", esp_err_to_name (err));
    }

  esp_http_client_cleanup (client);
}









void
Net_WIFI_mode_init (void)
{
  wifi_config_t sta_wifi_config = { };
  sprintf ((char *) sta_wifi_config.sta.ssid, "HEDY_2.4G");
  sprintf ((char *) sta_wifi_config.sta.password, "hedy0806");
  esp_wifi_set_mode (WIFI_MODE_STA);
  esp_wifi_set_config (ESP_IF_WIFI_STA, &sta_wifi_config);  // http client
  esp_wifi_start ();
}

void
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
        case WIFI_EVENT_STA_CONNECTED:
          ESP_LOGI(TAG, "wifi connected");
          break;
        case WIFI_EVENT_STA_DISCONNECTED:
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
