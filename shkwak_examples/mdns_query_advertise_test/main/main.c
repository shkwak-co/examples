#include <stdio.h>
#include <string.h>
#include "driver/gpio.h"

#include "mdns.h"

#include "nvs_flash.h"
#include "esp_wifi.h"
#include "esp_log.h"


// 간편한 wifi 연결을 위해 example_connect() 사용
#include "protocol_examples_common.h"


#define QUERY_BUTTON_GPIO 0             // boot 버튼
static const char *TAG = "mdns_test";

void Mdns_Init_Func (void);
void Mdns_Print_Results (mdns_result_t * results);

// 검색
void Query_mDNS_Hosted_Serive (const char *service_name, const char *proto);
void Query_mDNS_IP_using_Hostname (const char *host_name);
void Lookup_mDNS_Selfhosted_Service (const char *service_name, const char *proto);

// 버튼
void Button_Check (void);
void Button_Initialise (void);
void Button_Check_Task (void *pvParameters);

void
app_main (void)
{
  ESP_ERROR_CHECK (nvs_flash_init ());
  ESP_ERROR_CHECK (esp_netif_init ());
  ESP_ERROR_CHECK (esp_event_loop_create_default ());

  Mdns_Init_Func ();
  ESP_ERROR_CHECK (example_connect ());
  Button_Initialise ();
  xTaskCreate (&Button_Check_Task, "mdns button check", 1024 * 10, NULL, 5, NULL);
}

void
Mdns_Init_Func (void)
{
  char *hostname = strdup ("esp32-mdns");

  ESP_ERROR_CHECK (mdns_init ());
  ESP_ERROR_CHECK (mdns_hostname_set (hostname));
  ESP_ERROR_CHECK (mdns_instance_name_set ("ESP32 with mDNS"));

  mdns_txt_item_t serviceTxtData[3] = {
    {"board", "esp32"},
    {"t", "test"},
    {"text3", "HEDY"}
  };

  mdns_txt_item_t fake_service[2] = {
    {"text1", "hello"},
    {"text2", "world"}
  };

  /**
   * mDNS 서비스 등록
   * (장치가 호스팅하는 mDNS는 selfhosted service 검색으로 조회 가능)
  */
  mdns_service_add (NULL, "_testings", "_psudo", 81, NULL, 0);
  mdns_service_add (NULL, "_http", "_tcp", 80, NULL, 0);
  mdns_service_add (NULL, "_arduino", "_tcp", 3232, NULL, 0);
  mdns_service_add ("wrong type settings", "wrong", "wrong", 270, NULL, 0);
  /**
   * 서비스 instance_name 설정
  */
  mdns_service_instance_name_set ("_testings", "_psudo", "SHKwak's Service Testing");
  mdns_service_instance_name_set ("_http", "_tcp", "John's ESP32 Web Server");
  mdns_service_instance_name_set ("_arduino", "_tcp", "I'm not Arduino");

  /**
   * 서비스 TXT 설정
   * 아이템, 개수
  */
  mdns_service_txt_set ("_testings", "_psudo", serviceTxtData, 3);
  mdns_service_txt_set ("_arduino", "_tcp", fake_service, 2);
  mdns_service_txt_set ("_http", "_tcp", serviceTxtData, 3);
  mdns_service_txt_set ("wrong", "wrong", fake_service, 2);
  
  free (hostname);
}

/**
 * @brief results 출력
 * 
*/
static const char *ip_protocol_str[] = { "V4", "V6", "MAX" };

void
Mdns_Print_Results (mdns_result_t * results)
{
  mdns_result_t *r = results;
  mdns_ip_addr_t *a = NULL;
  int i = 1, t;
  while (r)
    {
      if (r->esp_netif)
        {
          printf ("%d: Interface: %s, Type: %s, TTL: %" PRIu32 "\n", i++, esp_netif_get_ifkey (r->esp_netif),
                  ip_protocol_str[r->ip_protocol], r->ttl);
        }
      a = r->addr;
      while (a)
        {
          if (a->addr.type == ESP_IPADDR_TYPE_V6)
            {
              printf ("  AAAA: " IPV6STR "\n", IPV62STR (a->addr.u_addr.ip6));
            }
          else
            {
              printf ("  A   : " IPSTR "\n", IP2STR (&(a->addr.u_addr.ip4)));
            }
          a = a->next;
        }
      if (r->instance_name)
        {
          printf ("  PTR : %s.%s.%s\n", r->instance_name, r->service_type, r->proto);
        }
      if (r->hostname)
        {
          printf ("  SRV : %s.local:%u\n", r->hostname, r->port);
        }
      if (r->txt_count)
        {
          printf ("  TXT : [%zu] ", r->txt_count);
          for (t = 0; t < r->txt_count; t++)
            {
              printf ("%s=%s(%d); ", r->txt[t].key, r->txt[t].value ? r->txt[t].value : "NULL", r->txt_value_len[t]);
            }
          printf ("\n");
        }
      r = r->next;
    }
}

/**
 * @brief 서비스 검색
 * 
 * @param 
 * service_name : 검색할 service_type 이름
 * 
 * proto        : 검색할 proto 이름
 * 
*/
void
Query_mDNS_Hosted_Serive (const char *service_name, const char *proto)
{
  ESP_LOGI (TAG, "Query PTR: %s.%s.local", service_name, proto);

  mdns_result_t *results = NULL;
  esp_err_t err = mdns_query_ptr (service_name, proto, 3000, 20, &results);
  if (err)
    {
      ESP_LOGE (TAG, "Query Failed: %s", esp_err_to_name (err));
      return;
    }
  if (!results)
    {
      ESP_LOGW (TAG, "No results found!");
      return;
    }

  Mdns_Print_Results (results);
  mdns_query_results_free (results);
}

/**
 * @brief self host 서비스 검색
 * 
 * @param
 * service_name : 검색할 selfhosted 서비스 타입 이름
 * 
 * proto        : 검색할 selfhosted 프로토콜 이름
 * 
*/
void
Lookup_mDNS_Selfhosted_Service (const char *service_name, const char *proto)
{
  ESP_LOGI (TAG, "Lookup selfhosted service: %s.%s.local", service_name, proto);
  mdns_result_t *results = NULL;
  esp_err_t err = mdns_lookup_selfhosted_service (NULL, service_name, proto, 20, &results);
  if (err)
    {
      ESP_LOGE (TAG, "Lookup Failed: %s", esp_err_to_name (err));
      return;
    }
  if (!results)
    {
      ESP_LOGW (TAG, "No results found!");
      return;
    }
  Mdns_Print_Results (results);
  mdns_query_results_free (results);
}


/**
 * @brief ip주소 query
 * 
 * @note
 * - host_name이 (name).local 형태인 경우 
 *                            에러메시지 출력
 * 
 * @param
 * host_name : 쿼리할 호스트 이름
*/
void
Query_mDNS_IP_using_Hostname (const char *host_name)
{
  ESP_LOGI (TAG, "Query A: %s.local", host_name);

  struct esp_ip4_addr addr;
  addr.addr = 0;

  esp_err_t err = mdns_query_a (host_name, 2000, &addr);
  if (err)
    {
      if (err == ESP_ERR_NOT_FOUND)
        {
          ESP_LOGW (TAG, "%s: Host was not found!", esp_err_to_name (err));
          return;
        }
      ESP_LOGE (TAG, "Query Failed: %s", esp_err_to_name (err));
      return;
    }

  printf ("  Query A: %s.local resolved to: " IPSTR, host_name, IP2STR (&addr));
  printf ("\r\n");
}

/**
 * @brief 버튼관련 설정
 * @note
 * - 버튼은 pin: 0으로 boot 버튼 사용
*/
void
Button_Initialise (void)
{
  gpio_config_t io_conf = { 0 };
  io_conf.intr_type = GPIO_INTR_DISABLE;
  io_conf.pin_bit_mask = BIT64 (QUERY_BUTTON_GPIO);
  io_conf.mode = GPIO_MODE_INPUT;
  io_conf.pull_up_en = 1;
  io_conf.pull_down_en = 0;
  gpio_config (&io_conf);
}

/**
 * @brief 버튼 입력시 동작 설정
 * 
 * @note
 * - self-querying 기능은 없으나 selfhosted_service에 대해서 검색은 가능함.
 * - service_type과 proto의 네이밍 규칙에는 이름은 _로 시작해야 하지만
 *                                      따르지 않더라도 검색 가능함. 
*/

/**  버튼 확인 방법
 *   old_level = 1 (누르지 않음)
 *   new_level = 버튼상태 확인 (누르면 0, 누르지 않으면 1)
 *   old와 !new(new의 반대)를 &&(AND연산)하여 상태 확인
 * 
 *  old  |  new  |  !new  |  !new && old
 *   1   |   1   |    0   |       0
 *   1   |   0   |    1   |       1   (버튼입력)
 *   0   |   1   |    0   |       0
 *   1   |   1   |    0   |       0
 *   1   |   0   |    1   |       1   (버튼입력)
 *   0   |   1   |    0   |       0
 *   1   |   1   |    0   |       0
 *   1   |   1   |    0   |       0
*/
void
Button_Check (void)
{
  static bool old_level = true;
  bool new_level = gpio_get_level (QUERY_BUTTON_GPIO);
  if (!new_level && old_level)
    {

      //host 이름으로 query
      // Query_mDNS_IP_using_Hostname ("esp32");
      // Query_mDNS_IP_using_Hostname ("esp32-mdns");              // self-querying 불가능
      Query_mDNS_IP_using_Hostname ("wifibridge");
      Query_mDNS_IP_using_Hostname ("SEC842519B83B5B");
      Query_mDNS_IP_using_Hostname ("wifibridge-002");
      // Query_mDNS_IP_using_Hostname ("");                        // INVALID_ARG

      // mDNS 서비스 정보로 검색
      Query_mDNS_Hosted_Serive ("_arduino", "_tcp");
      Query_mDNS_Hosted_Serive ("_http", "_tcp");
      Query_mDNS_Hosted_Serive ("_printer", "_tcp");
      Query_mDNS_Hosted_Serive ("_ipp", "_tcp");

      // 셀프 호스팅한 서비스 검색
      Lookup_mDNS_Selfhosted_Service ("_http", "_tcp");
      Lookup_mDNS_Selfhosted_Service ("_arduino", "_tcp");
      Lookup_mDNS_Selfhosted_Service ("_printer", "_tcp");
      Lookup_mDNS_Selfhosted_Service ("_testings", "_psudo");
      Lookup_mDNS_Selfhosted_Service ("wrong", "wrong");
    }
  old_level = new_level;
}

void
Button_Check_Task (void *pvParameters)
{
  while (1)
    {
      Button_Check ();
      vTaskDelay (50 / portTICK_PERIOD_MS);
    }
}
