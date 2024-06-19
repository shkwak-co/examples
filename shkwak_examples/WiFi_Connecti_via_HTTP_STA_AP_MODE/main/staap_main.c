// 
#include <stdio.h>
#include <sys/param.h>
// freeRTOS
#include "freertos/FreeRTOSConfig.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/timers.h"
#include "freertos/event_groups.h"
// esp
#include "esp_event.h"
#include "esp_wifi.h"
#include "esp_err.h"
#include "esp_http_server.h"
#include "esp_sleep.h"
#include "esp_log.h"
//
#include "lwip/err.h"
#include "nvs_flash.h"
// 
#include "cJSON.h"
//
#include <driver/gpio.h>

static const char *TAG = "APPLICATION";

#define LED_PIN_1 5
#define LED_PIN_2 17

/*
  RTC_DATA_ATTR을 사용하여 RTC에 저장해놓고
  sleep에도 삭제되지 않도록 하여 전달
*/
static RTC_DATA_ATTR char __SSID[32];
static RTC_DATA_ATTR char __PWD[64];
//static RTC_DATA_ATTR char GOT_IP = false;
static RTC_DATA_ATTR char GOT_IP;


#define SOFTAP_ESP_SSID   "ESP_AP_SSID"
#define SOFTAP_ESP_PWD    "12345678"

EventGroupHandle_t wifi_event_group;
#define WIFI_CONNECTED_BIT    BIT0
#define PERIOD_CHANGE_BIT     BIT1

/**
 * @running_value       : GET명령어로 얻을 수 있는 계속 변하는 변수값
 * @connection_flag     : connection 상태에 대한 flag
 * @getPeriod           : LED 주기 변경을 위해 web에서 사용자에게 받는 값을 저장하는 변수
*/
int running_value = 0;
volatile char connection_flag = 0;
volatile int getPeriod = 1000;

TimerHandle_t LED_Timer_handler = NULL;
TimerHandle_t LED_Connect_Timer = NULL;
TaskHandle_t xtask1 = NULL;
TaskHandle_t xtask2 = NULL;

/**
 * @event_handler               : 발생한 event에 따라 동작
 * @mainPage_get_handler        : 192.166.4.1/ 구성
 * @change_handler              : LED 주기 변경 동작
 * @psw_ssid_get_handler        : ssid, psw 값 받아오는 동작
 * @getValue_get_handler        : running_value web에 출력해줌
 * @web_server_start            : web서버 시작
 * @Start_AP_or_STA_Mode        : wifi 정보 받아왔으면 STA모드, 아니면 AP모드 시작
 * @VLED_Period                 : led 주기 변경하는 timer callback function
 * @vTask_running_value         : 계속 돌면서 running_value 값 변경
 * @vPeriod_change_task         : LED주기 변경값을 받으면 timer 주기 변경
 * @vLED_Connect_Timercallback  : connection 상태에 따라 주기 변경
 * 
*/
void event_handler (void *arg, esp_event_base_t event_base, int32_t event_id, void *event_data);
static esp_err_t mainPage_get_handler (httpd_req_t * req);
static esp_err_t change_handler (httpd_req_t * req);
static esp_err_t psw_ssid_get_handler (httpd_req_t * req);
static esp_err_t getValue_get_handler (httpd_req_t * req);
static httpd_handle_t web_server_start (void);
void Start_AP_or_STA_Mode ();
void vLED_Period_Timercallback (void *pvParam);
void vTask_running_value (void *pvParam);
void vPeriod_change_task (void *pvParam);
void vLED_Connect_Timercallback (void *pvParam);

void
app_main (void)
{
/**
 * led 설정
 * @LED_PIN_1 : 주기변경에 사용
 * @LED_PIN_2 : Connect/Disconnect 표시에 사용
*/
  gpio_reset_pin (LED_PIN_1);
  gpio_set_direction (LED_PIN_1, GPIO_MODE_OUTPUT);
  gpio_reset_pin (LED_PIN_2);
  gpio_set_direction (LED_PIN_2, GPIO_MODE_OUTPUT);

  //nvs 설정
  esp_err_t ret = nvs_flash_init ();
  if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND)
    {
      ESP_ERROR_CHECK (nvs_flash_erase ());
      ret = nvs_flash_init ();
    }
  ESP_ERROR_CHECK (ret);

  // event loop 설정
  ESP_ERROR_CHECK (esp_event_loop_create_default ());

  // 네트워크 인터페이스 설정
  ESP_ERROR_CHECK (esp_netif_init ());

  // sta, ap 생성
  /*
     기본 sta, ap 구성으로 esp_netif 객체 생성.
     WIFI EVENT를 default event loop에 등록함.
   */
  esp_netif_create_default_wifi_sta ();
  esp_netif_create_default_wifi_ap ();

  // 이벤트 그룹 설정
  wifi_event_group = xEventGroupCreate ();

  wifi_init_config_t wifi_init_cfg = WIFI_INIT_CONFIG_DEFAULT ();
  ESP_ERROR_CHECK (esp_wifi_init (&wifi_init_cfg));

  ESP_ERROR_CHECK (esp_event_handler_register (WIFI_EVENT, ESP_EVENT_ANY_ID, &event_handler, NULL));
  ESP_ERROR_CHECK (esp_event_handler_register (IP_EVENT, IP_EVENT_STA_GOT_IP, &event_handler, NULL));

  /**
   * task, timer 생성
   * @task1 : running_value 값 변경시켜주는 task. period값도 뿌려줌
   * @task2 : 이벤트 비트가 설정되면 timer 주기 변경해줌. 
  */
  xTaskCreate (vTask_running_value, "task 1", 1024 * 2, NULL, 1, &xtask1);
  xTaskCreate (vPeriod_change_task, "task 2", 1024 * 2, NULL, 5, &xtask2);
  LED_Timer_handler = xTimerCreate ("led period", pdMS_TO_TICKS (getPeriod), pdTRUE, 0, vLED_Period_Timercallback);
  LED_Connect_Timer = xTimerCreate ("connect period", pdMS_TO_TICKS (300UL), pdTRUE, 0, vLED_Connect_Timercallback);
  xTimerStart (LED_Timer_handler, 0);

  // AP or STA mode 선택하여 start
  Start_AP_or_STA_Mode ();
  // wait for wifi connection
  xEventGroupWaitBits (wifi_event_group, WIFI_CONNECTED_BIT, pdFALSE, pdFALSE, portMAX_DELAY);

  /**
   * wifi 연결되면 LED는 연결/비연결 상태를 시각적으로 보여줌
  */
  int connect_led_period = 0;
  uint8_t status = false;
  while (1)
    {
      vTaskDelay (pdMS_TO_TICKS (1000UL));
      printf ("%s", connection_flag > 0 ? "connected\r\n" : "disconnected\r\n");
      if (connection_flag != 0)
        {
          if (status == false)
            {
              connect_led_period = 2000;
              xTimerChangePeriod (LED_Connect_Timer, pdMS_TO_TICKS (connect_led_period), portMAX_DELAY);
              status = true;
            }
        }
      else
        {
          if (status == true)
            {
              connect_led_period = 300;
              xTimerChangePeriod (LED_Connect_Timer, pdMS_TO_TICKS (connect_led_period), portMAX_DELAY);
              status = false;
            }
        }
    }
}

/**
 * @WIFI_EVENT_STA_START          : STA모드 시작
 * @WIFI_EVENT_STA_DISCONNECTED   : AP에 연결 실패
 * @WIFI_EVENT_AP_STACONNECTED    : 클라이언트 연결됨
 * @WIFI_EVENT_AP_STADISCONNECTED : 클라이언트 연결 끊김
 * 
 * @IP_eVENT_STA_GOT_AP           : 연결된 AP로부터 IP를 받음
 * 
*/
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
        case WIFI_EVENT_STA_DISCONNECTED:
          /*
             잘못된 정보 입력 (비밀번호가 틀리다던지)에 대한
             에러처리에 관해서도 학습해서 추가 해 볼 것
           */
          ESP_LOGI (TAG, "Disconnected. Retrying...");
          connection_flag = 0;
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
          xEventGroupSetBits (wifi_event_group, WIFI_CONNECTED_BIT);
          connection_flag = 1;
          break;
        }
    }
}

static esp_err_t
mainPage_get_handler (httpd_req_t * req)
{
  ESP_LOGI (TAG, "WiFi info");
  // html 문서 시작
  httpd_resp_sendstr_chunk (req, "<!DOCTYPE html><html>");
  // html 헤드
  httpd_resp_sendstr_chunk (req, "<head>");
  httpd_resp_sendstr_chunk (req, "<style>");
  // css 스타일 지정
  httpd_resp_sendstr_chunk (req,
                            "form {display: grid;padding: 1em; background: #f9f9f9; border: 1px solid #c1c1c1; margin: 2rem auto 0 auto; max-width: 400px; padding: 1em;}");
  httpd_resp_sendstr_chunk (req, "form input {background: #fff;border: 1px solid #9c9c9c;}");
  httpd_resp_sendstr_chunk (req, "form button {background: lightgrey; padding: 0.7em;width: 100%; border: 0;");
  httpd_resp_sendstr_chunk (req, "label {padding: 0.5em 0.5em 0.5em 0;}");
  httpd_resp_sendstr_chunk (req, "input {padding: 0.7em;margin-bottom: 0.5rem;}");
  httpd_resp_sendstr_chunk (req, "input:focus {outline: 10px solid gold;}");
  httpd_resp_sendstr_chunk (req,
                            "@media (min-width: 300px) {form {grid-template-columns: 200px 1fr; grid-gap: 16px;} label { text-align: right; grid-column: 1 / 2; } input, button { grid-column: 2 / 3; }}");
  httpd_resp_sendstr_chunk (req,
                            "form {display: grid;padding: 1em; background: #f9f9f9; border: 1px solid #c1c1c1; margin: 2rem auto 0 auto; max-width: 400px; padding: 1em;}");
  httpd_resp_sendstr_chunk (req, "form input {background: #fff;border: 1px solid #9c9c9c;}");
  httpd_resp_sendstr_chunk (req, "form button {background: lightgrey; padding: 0.7em;width: 100%; border: 0;");
  httpd_resp_sendstr_chunk (req, "label {padding: 0.5em 0.5em 0.5em 0;}");
  httpd_resp_sendstr_chunk (req, "input {padding: 0.7em;margin-bottom: 0.5rem;}");
  httpd_resp_sendstr_chunk (req, "input:focus {outline: 10px solid gold;}");
  httpd_resp_sendstr_chunk (req,
                            "@media (min-width: 300px) {form {grid-template-columns: 200px 1fr; grid-gap: 16px;} label { text-align: right; grid-column: 1 / 2; } input, button { grid-column: 2 / 3; }}");
  httpd_resp_sendstr_chunk (req, "</style>");



  httpd_resp_sendstr_chunk (req, "</head>");

  //html 바디
  httpd_resp_sendstr_chunk (req, "<body>");

  // form 1 script(wifi connect)
  httpd_resp_sendstr_chunk (req, "<form class=\"form1\" id=\"loginForm\" action=\"\">");

  // wifi 이름 입력
  httpd_resp_sendstr_chunk (req, "<label for=\"SSID\">WiFi Name</label>");
  httpd_resp_sendstr_chunk (req, "<input id=\"ssid\" type=\"text\" name=\"ssid\" maxlength=\"64\" minlength=\"4\">");
  //wifi 비밀번호 입력
  httpd_resp_sendstr_chunk (req, "<label for=\"Password\">Password</label>");
  httpd_resp_sendstr_chunk (req, "<input id=\"pwd\" type=\"password\" name=\"pwd\" maxlength=\"64\" minlength=\"4\">");
  // submit 버튼 구현
  httpd_resp_sendstr_chunk (req, "<button>Submit</button>");
  httpd_resp_sendstr_chunk (req, "</form>");
  // javascript 스크립트
  httpd_resp_sendstr_chunk (req, "<script>");
  // ssid, password값 관련
  httpd_resp_sendstr_chunk (req,
                            "document.getElementById(\"loginForm\").addEventListener(\"submit\", (e) => {e.preventDefault(); const formData = new FormData(e.target); const data = Array.from(formData.entries()).reduce((memo, pair) => ({...memo, [pair[0]]: pair[1],  }), {}); var xhr = new XMLHttpRequest(); xhr.open(\"POST\", \"http://192.168.4.1/connection\", true); xhr.setRequestHeader('Content-Type', 'application/json'); xhr.send(JSON.stringify(data)); document.getElementById(\"output\").innerHTML = JSON.stringify(data);});");
  httpd_resp_sendstr_chunk (req, "</script>");


  // form 2 script(period change)
  httpd_resp_sendstr_chunk (req, "<form class=\"form2\" id=\"changeForm\" action=\"/change\">");
  httpd_resp_sendstr_chunk (req, "<label for=\"numberInput\">Period (1-10000)</label>");
  httpd_resp_sendstr_chunk (req,
                            " <input id=\"numberInput\" type=\"number\" name=\"numberInput\" placeholder=\"ms\" min=\"1\" max=\"10000\" value=\"1000\">");
  httpd_resp_sendstr_chunk (req, "<button>Change</button>");
  httpd_resp_sendstr_chunk (req, "</form>");

  // 내용
  httpd_resp_sendstr_chunk (req, "<script>");
  httpd_resp_sendstr_chunk (req,
                            "document.getElementById(\"changeForm\").addEventListener(\"submit\", async (e) => {e.preventDefault();");
  httpd_resp_sendstr_chunk (req, "const formData = new FormData(e.target);");
  httpd_resp_sendstr_chunk (req, "const numberInput = formData.get('numberInput');");
  httpd_resp_sendstr_chunk (req,
                            "if (numberInput >= 1 && numberInput <= 10000) {const data = { numberInput: parseInt(numberInput, 10) };");
  httpd_resp_sendstr_chunk (req,
                            "try {const response = await fetch(\"http://192.168.4.1/change\", {method: \"POST\",headers: {'Content-Type': 'application/json'}, ");
  httpd_resp_sendstr_chunk (req, "body: JSON.stringify(data)});");
  httpd_resp_sendstr_chunk (req,
                            "if (response.ok) { document.getElementById(\"numberInput\").value = numberInput;document.getElementById(\"output\").innerHTML = JSON.stringify(data);} ");
  httpd_resp_sendstr_chunk (req,
                            "else {alert(\"failed to send.\");}} catch (error) {console.error(\"err:\", error);}} else {alert(\"1~10000.\");}});");

  httpd_resp_sendstr_chunk (req, "</script>");
  httpd_resp_sendstr_chunk (req, "</body>");
  httpd_resp_sendstr_chunk (req, "</html>");

  return ESP_OK;
}

static const httpd_uri_t mainPage = {
  .uri = "/",
  .method = HTTP_GET,
  .handler = mainPage_get_handler,
  .user_ctx = NULL
};

static esp_err_t
change_handler (httpd_req_t * req)
{
  // 요청 본문에서 데이터 읽기
  int content_length = req->content_len;
  char *buffer = (char *) malloc (content_length);
  if (!buffer)
    {
      httpd_resp_send_err (req, HTTPD_500_INTERNAL_SERVER_ERROR, "Failed to allocate memory");
      return ESP_FAIL;
    }

  if (httpd_req_recv (req, buffer, content_length) <= 0)
    {
      free (buffer);
      httpd_resp_send_err (req, HTTPD_500_INTERNAL_SERVER_ERROR, "Failed to receive data");
      return ESP_FAIL;
    }
  // 받은 데이터를 JSON 형식으로 출력
  ESP_LOGI (TAG, "Received data: %.*s", content_length, buffer);
  cJSON *root = cJSON_Parse (buffer);
  if (!root)
    {
      free (buffer);
      httpd_resp_send_err (req, HTTPD_500_INTERNAL_SERVER_ERROR, "Failed to parse JSON data");
      return ESP_FAIL;
    }

  // JSON 데이터에서 숫자 값 얻어오기
  cJSON *numberInputJSON = cJSON_GetObjectItem (root, "numberInput");
  if (!numberInputJSON || !cJSON_IsNumber (numberInputJSON))
    {
      free (buffer);
      cJSON_Delete (root);
      httpd_resp_send_err (req, HTTPD_400_BAD_REQUEST, "Invalid numberInput value in JSON");
      return ESP_FAIL;
    }

  // 숫자 값 얻어와서 변수에 저장
  getPeriod = numberInputJSON->valueint;
  // 메모리와 JSON 구조체 해제
  free (buffer);
  cJSON_Delete (root);
  // 응답
  httpd_resp_set_type (req, "text/plain");
  httpd_resp_send (req, "OK", 2);
  xEventGroupSetBits (wifi_event_group, PERIOD_CHANGE_BIT);
  return ESP_OK;
}

httpd_uri_t change_uri = {
  .uri = "/change",
  .method = HTTP_POST,
  .handler = change_handler,
};

static esp_err_t
psw_ssid_get_handler (httpd_req_t * req)
{
  char buf[128];
  int ret, remaining = req->content_len;
  while (remaining > 0)
    {
      /* Read the data for the request */
      if ((ret = httpd_req_recv (req, buf, MIN (remaining, sizeof (buf)))) <= 0)
        {
          if (ret == 0)
            {
              ESP_LOGI (TAG, "No content received please try again ...");
            }
          else if (ret == HTTPD_SOCK_ERR_TIMEOUT)
            {
              /* Retry receiving if timeout occurred */
              continue;
            }
          return ESP_FAIL;
        }

      /* Log data received */
      ESP_LOGI (TAG, "=========== RECEIVED DATA ==========");
      ESP_LOGI (TAG, "%.*s", ret, buf);
      ESP_LOGI (TAG, "====================================");
      cJSON *root = cJSON_Parse (buf);
      sprintf (__SSID, "%s", cJSON_GetObjectItem (root, "ssid")->valuestring);
      sprintf (__PWD, "%s", cJSON_GetObjectItem (root, "pwd")->valuestring);
      ESP_LOGI (TAG, "pwd: %s", __PWD);
      ESP_LOGI (TAG, "ssid: %s", __SSID);
      remaining -= ret;
    }

  // End response
  httpd_resp_send_chunk (req, NULL, 0);
  GOT_IP = true;
  /* 
     RTC에 저장하지 말고, NVS를 사용하며
     정보를 받으면 NVS에 정보 저장 후
     Sleep 하는 방식 말고, 받은 정보로 연결 시도하는 방법 생각해보기
   */
  esp_sleep_enable_timer_wakeup (100000);
  esp_deep_sleep_start ();
  return ESP_OK;
}

static const httpd_uri_t psw_ssid = {
  .uri = "/connection",
  .method = HTTP_POST,
  .handler = psw_ssid_get_handler,
  .user_ctx = "TEST"
};

static esp_err_t
getValue_get_handler (httpd_req_t * req)
{
  char response_str[64];
  snprintf (response_str, sizeof (response_str), "Value : %d", running_value);
  httpd_resp_send (req, response_str, HTTPD_RESP_USE_STRLEN);
  ESP_LOGI (TAG, "send Value");
  return ESP_OK;
}

static const httpd_uri_t getValue = {
  .uri = "/getValue",
  .method = HTTP_GET,
  .handler = getValue_get_handler,
  .user_ctx = NULL
};

// server uri regist
static httpd_handle_t
web_server_start (void)
{
  httpd_handle_t server = NULL;
  httpd_config_t config = HTTPD_DEFAULT_CONFIG ();
  // Start the httpd server
  ESP_LOGI (TAG, "Starting server on port: '%d'", config.server_port);
  if (httpd_start (&server, &config) == ESP_OK)
    {
      // Set URI handlers
      ESP_LOGI (TAG, "Registering URI handlers");
      httpd_register_uri_handler (server, &mainPage);
      httpd_register_uri_handler (server, &psw_ssid);
      httpd_register_uri_handler (server, &getValue);
      httpd_register_uri_handler (server, &change_uri);
      return server;
    }

  ESP_LOGI (TAG, "Error starting server!");
  return NULL;
}

/* 
ap 동작후 wifi 정보 받으면 apsta
void
Start_AP_or_STA_Mode ()
{
  wifi_config_t wifi_ap_config = {
    .ap = {
           .ssid = SOFTAP_ESP_SSID,
           .ssid_len = strlen (SOFTAP_ESP_SSID),
           .channel = 1,.password = SOFTAP_ESP_PWD,
           .max_connection = 4,
           .authmode = WIFI_AUTH_WPA2_PSK,
           .pmf_cfg = {
                       .required = true,
                       }
           }
  };
  ESP_ERROR_CHECK (esp_wifi_set_config (WIFI_IF_AP, &wifi_ap_config));

  //#2 web_server_start ();
  if (GOT_IP == false)
    {
      ESP_ERROR_CHECK (esp_wifi_set_mode (WIFI_MODE_AP));
      ESP_ERROR_CHECK (esp_wifi_start ());
    }
  else if (GOT_IP == true)
    {
      wifi_config_t wifi_sta_config = {
      };
      strcpy ((char *) wifi_sta_config.sta.ssid, __SSID);
      strcpy ((char *) wifi_sta_config.sta.password, __PWD);
      ESP_LOGI (TAG, "WiFi %s ", wifi_sta_config.sta.ssid);
      ESP_LOGI (TAG, "PSW %s ", wifi_sta_config.sta.password);
      //ESP_ERROR_CHECK (esp_wifi_set_mode (WIFI_MODE_STA));
      //#2 STA AP 모드 ( APSTA + STA )
      ESP_ERROR_CHECK (esp_wifi_set_mode (WIFI_MODE_APSTA));
      ESP_ERROR_CHECK (esp_wifi_set_config (ESP_IF_WIFI_STA, &wifi_sta_config));
      ESP_ERROR_CHECK (esp_wifi_start ());
      xTimerStart (LED_Connect_Timer, 0);
    }
  web_server_start ();
}
 */

void
Start_AP_or_STA_Mode ()
{
  wifi_config_t wifi_ap_config = {
    .ap = {
           .ssid = SOFTAP_ESP_SSID,
           .ssid_len = strlen (SOFTAP_ESP_SSID),
           .channel = 1,.password = SOFTAP_ESP_PWD,
           .max_connection = 4,
           .authmode = WIFI_AUTH_WPA2_PSK,
           //Protected Management Frame(PMF) : 보안 관리 프레임
           .pmf_cfg = {
                       .required = true,
                       }
           }
  };
  wifi_config_t wifi_sta_config = { };
  strcpy ((char *) wifi_sta_config.sta.ssid, __SSID);
  strcpy ((char *) wifi_sta_config.sta.password, __PWD);
  ESP_LOGI (TAG, "WiFi %s ", wifi_sta_config.sta.ssid);
  ESP_LOGI (TAG, "PSW %s ", wifi_sta_config.sta.password);

  ESP_ERROR_CHECK (esp_wifi_set_mode (WIFI_MODE_APSTA));
  ESP_ERROR_CHECK (esp_wifi_set_config (ESP_IF_WIFI_AP, &wifi_ap_config));
  ESP_ERROR_CHECK (esp_wifi_set_config (ESP_IF_WIFI_STA, &wifi_sta_config));
  ESP_ERROR_CHECK (esp_wifi_start ());
  xTimerStart (LED_Connect_Timer, 0);

  web_server_start ();
}

/**
 * timer callback 함수
 * led_status에 맞춰 on/off 시켜줌
*/
void
vLED_Period_Timercallback (void *pvParam)
{
  static uint8_t led_status = false;
  led_status = !led_status;
  gpio_set_level (LED_PIN_1, led_status);
}

void
vLED_Connect_Timercallback (void *pvParam)
{
  static uint8_t led_status = false;
  led_status = !led_status;
  gpio_set_level (LED_PIN_2, led_status);
}

void
vTask_running_value (void *pvParam)
{
  while (1)
    {
      if (running_value < 50)
        running_value++;
      else
        {
          running_value = 0;
          printf ("period = %d\r\n", getPeriod);
        }
      vTaskDelay (pdMS_TO_TICKS (100));
    }
}

void
vPeriod_change_task (void *pvParam)
{
  static int temp = 1000;
  while (1)
    {

      xEventGroupWaitBits (wifi_event_group, PERIOD_CHANGE_BIT, pdTRUE, pdFALSE, portMAX_DELAY);
      if (temp != getPeriod)
        {
          temp = getPeriod;
          printf ("change! %d\r\n", temp);
          xTimerChangePeriod (LED_Timer_handler, pdMS_TO_TICKS (temp), portMAX_DELAY);
        }
    }
}
