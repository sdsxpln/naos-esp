#include <esp_err.h>
#include <esp_log.h>
#include <esp_ota_ops.h>
#include <esp_system.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

#include "general.h"
#include "update.h"

static SemaphoreHandle_t nadk_update_mutex;

static const esp_partition_t *nadk_update_partition = NULL;
static esp_ota_handle_t nadk_update_handle = 0;

void nadk_update_init() {
  // create mutex
  nadk_update_mutex = xSemaphoreCreateMutex();
}

void nadk_update_begin(uint16_t size) {
  // acquire mutex
  NADK_LOCK(nadk_update_mutex);

  // check handle
  if (nadk_update_handle != 0) {
    ESP_LOGE(NADK_LOG_TAG, "nadk_update_begin: leftover handle");
    NADK_UNLOCK(nadk_update_mutex);
    return;
  }

  // log message
  ESP_LOGI(NADK_LOG_TAG, "nadk_update_begin: start update");

  // get update partition
  nadk_update_partition = esp_ota_get_next_update_partition(NULL);
  assert(nadk_update_partition != NULL);

  // begin update
  ESP_ERROR_CHECK(esp_ota_begin(nadk_update_partition, 0, &nadk_update_handle));

  // release mutex
  NADK_UNLOCK(nadk_update_mutex);
}

void nadk_update_write(const char *chunk, uint16_t len) {
  // acquire mutex
  NADK_LOCK(nadk_update_mutex);

  // check handle
  if (nadk_update_handle == 0) {
    ESP_LOGE(NADK_LOG_TAG, "nadk_update_write: missing handle");
    NADK_UNLOCK(nadk_update_mutex);
    return;
  }

  // write chunk
  ESP_ERROR_CHECK(esp_ota_write(nadk_update_handle, (const void *)chunk, len));

  // release mutex
  NADK_UNLOCK(nadk_update_mutex);
}

void nadk_update_finish() {
  // acquire mutex
  NADK_LOCK(nadk_update_mutex);

  // check handle
  if (nadk_update_handle == 0) {
    ESP_LOGE(NADK_LOG_TAG, "nadk_update_finish: missing handle");
    NADK_UNLOCK(nadk_update_mutex);
    return;
  }

  // end update
  ESP_ERROR_CHECK(esp_ota_end(nadk_update_handle));

  // reset handle
  nadk_update_handle = 0;

  // set boot partition
  ESP_ERROR_CHECK(esp_ota_set_boot_partition(nadk_update_partition));

  // log message
  ESP_LOGI(NADK_LOG_TAG, "nadk_update_finish: update finished");

  // release mutex
  NADK_UNLOCK(nadk_update_mutex);

  // restart system
  esp_restart();
}