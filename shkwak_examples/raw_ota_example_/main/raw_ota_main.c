#include <stdio.h>

#include "esp_ota_ops.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "esp_system.h"
#include "esp_log.h"

#define APP_VALID_STATE         // 테스트용
#define OTA_STATE_CHECK 1       // 1: pending verify 2: valid 3: invalid

void OTA_function ();
void Running_task (void *arg);
void Running_task2 (void *arg);


const static char *TAG = "raw_ota_example";

#if OTA_STATE_CHECK == 1
extern const unsigned char monitor_html_gz_start[] asm ("_binary_hello_world_pv_bin_start");
extern const unsigned char monitor_html_gz_end[] asm ("_binary_hello_world_pv_bin_end");
#elif OTA_STATE_CHECK == 2
extern const unsigned char monitor_html_gz_start[] asm ("_binary_hello_world_val_bin_start");
extern const unsigned char monitor_html_gz_end[] asm ("_binary_hello_world_val_bin_end");
#elif OTA_STATE_CHECK == 3
extern const unsigned char monitor_html_gz_start[] asm ("_binary_hello_world_inv_bin_start");
extern const unsigned char monitor_html_gz_end[] asm ("_binary_hello_world_inv_bin_end");
#endif


void
Running_task (void *arg)
{
  vTaskDelay (pdMS_TO_TICKS (3000UL));
  while (1)
    {
      printf ("task1 working!\r\n");
      vTaskDelay (pdMS_TO_TICKS (50UL));
    }
}

void
Running_task2 (void *arg)
{
  vTaskDelay (pdMS_TO_TICKS (3000UL));
  while (1)
    {
      printf ("              task2 working!\r\n");
      vTaskDelay (pdMS_TO_TICKS (120UL));
    }
}

 
void
OTA_function ()
{
  ESP_LOGI (TAG, "OTA start");
  size_t monitor_html_gz_len = monitor_html_gz_end - monitor_html_gz_start; // 바이너리 파일 size

  size_t partial_size = 1024;   // 한번에 업데이트 할 size
  esp_ota_handle_t ota_handle;  // ota 업데이트 할 handle
  const esp_partition_t *next_partition = esp_ota_get_next_update_partition (NULL); // 업데이트 할 파티션

  ESP_LOGI (TAG, "OTA begin");
  esp_ota_begin (next_partition, OTA_SIZE_UNKNOWN, &ota_handle);


  ESP_LOGI (TAG, "OTA write");
  size_t bytes_written = 0;     // 작성된 사이즈(바이트 수)
  while (bytes_written < monitor_html_gz_len)
    {
      // 작성한 사이즈.  남은 사이즈가 업데이트 단위보다 적게 남았으면 적은만큼만 작성
      size_t write_size =
        (monitor_html_gz_len - bytes_written) < partial_size ? (monitor_html_gz_len - bytes_written) : partial_size;

      //                    핸들     ,   시작점 (처음 + 이미 작성한 만큼 부터)  , 작성할 크기
      esp_ota_write (ota_handle, monitor_html_gz_start + bytes_written, write_size);
      bytes_written += write_size;
      vTaskDelay (pdMS_TO_TICKS (10UL));
    }

  ESP_LOGI (TAG, "OTA end");
  // OTA 업데이트 종료
  esp_ota_end (ota_handle);


  ESP_LOGI (TAG, "OTA set boot partition");
  // 새 이미지를 부팅 파티션으로 설정
  esp_ota_set_boot_partition (next_partition);
  ESP_LOGI (TAG, "OTA update successful");
}

void
app_main (void)
{
  const esp_partition_t *running_partition = esp_ota_get_running_partition ();
  esp_ota_img_states_t ota_state;

  esp_ota_get_state_partition (running_partition, &ota_state);

  // ota 기능 사용시 새로 업데이트 할 펌웨어에는 이 코드가 있어야 함.
  if (ota_state == ESP_OTA_IMG_PENDING_VERIFY)
    {
      // test 동작 구현
      // 네트워크 연결 등등 잘 되는지 확인하는 코드
#ifdef APP_VALID_STATE
      // test 성공시
      esp_ota_mark_app_valid_cancel_rollback ();
#else
      // test 실패시
      esp_ota_mark_app_invalid_rollback_and_reboot ();
#endif
    }

  xTaskCreate (Running_task, "task 1", 1024 * 2, NULL, 10, NULL);
  xTaskCreate (Running_task2, "task 2", 1024 * 2, NULL, 12, NULL);
  OTA_function ();
  // 업데이트 후 재부팅
  esp_restart ();
}
