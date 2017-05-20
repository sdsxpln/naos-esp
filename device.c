#include <esp_log.h>
#include <esp_system.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <freertos/task.h>
#include <string.h>

#include <nadk.h>

#include "ble.h"
#include "device.h"
#include "general.h"
#include "ota.h"

#define NADK_OTA_CHUNK_SIZE 9000

#define NADK_DEVICE_HEARTBEAT_INTERVAL 5000

static SemaphoreHandle_t nadk_device_mutex;

static nadk_device_t *nadk_device;

static TaskHandle_t nadk_device_task;

static bool nadk_device_process_started = false;

static uint32_t nadk_device_last_heartbeat = 0;

static long long int nadk_device_ota_remaining_data = 0;

// TODO: Add offline device loop.
// The callbacks could be: offline, connected, online, disconnected.

// TODO: Rename "ota" topic segments to "update".

static void nadk_device_send_heartbeat() {
  // send device name
  char device_name[NADK_BLE_STRING_SIZE];
  nadk_ble_get_string(NADK_BLE_ID_DEVICE_NAME, device_name);

  // save time
  nadk_device_last_heartbeat = nadk_millis();

  // send heartbeat
  char buf[64];
  snprintf(buf, sizeof buf, "%s,%s,%s,%d,%d", nadk_device->type, nadk_device->version, device_name,
           esp_get_free_heap_size(), nadk_millis());
  nadk_publish_str("nadk/heartbeat", buf, 0, false, NADK_LOCAL);
}

static void nadk_device_send_announcement() {
  // get device name
  char device_name[NADK_BLE_STRING_SIZE];
  nadk_ble_get_string(NADK_BLE_ID_DEVICE_NAME, device_name);

  // get base topic
  char base_topic[NADK_BLE_STRING_SIZE];
  nadk_ble_get_string(NADK_BLE_ID_BASE_TOPIC, base_topic);

  // send announce
  char buf[64];
  snprintf(buf, sizeof buf, "%s,%s,%s,%s", nadk_device->type, nadk_device->version, device_name, base_topic);
  nadk_publish_str("nadk/announcement", buf, 0, false, NADK_GLOBAL);
}

static void nadk_device_request_next_chunk() {
  // calculate next chunk
  int chunk = NADK_OTA_CHUNK_SIZE;
  if (nadk_device_ota_remaining_data < chunk) {
    chunk = (int)nadk_device_ota_remaining_data;
  }

  // request first chunk
  nadk_publish_num("nadk/ota/next", chunk, 0, false, NADK_LOCAL);
}

static void nadk_device_process(void *p) {
  // acquire mutex
  NADK_LOCK(nadk_device_mutex);

  // subscribe to global topics
  nadk_subscribe("nadk/collect", 0, NADK_GLOBAL);

  // subscribe to device topics
  nadk_subscribe("nadk/ping", 0, NADK_LOCAL);
  nadk_subscribe("nadk/ota", 0, NADK_LOCAL);
  nadk_subscribe("nadk/ota/chunk", 0, NADK_LOCAL);

  // call setup callback i present
  if (nadk_device->setup_fn) {
    nadk_device->setup_fn();
  }

  // send initial heartbeat
  nadk_device_send_heartbeat();

  // release mutex
  NADK_UNLOCK(nadk_device_mutex);

  for (;;) {
    // acquire mutex
    NADK_LOCK(nadk_device_mutex);

    // send heartbeat if interval has been reached
    if (nadk_millis() - nadk_device_last_heartbeat > NADK_DEVICE_HEARTBEAT_INTERVAL) {
      nadk_device_send_heartbeat();
    }

    // call loop callback if present
    if (nadk_device->loop_fn) {
      nadk_device->loop_fn();
    }

    // release mutex
    NADK_UNLOCK(nadk_device_mutex);

    // yield to other processes
    vTaskDelay(1);
  }
}

void nadk_device_init(nadk_device_t *device) {
  // set device reference
  nadk_device = device;

  // create mutex
  nadk_device_mutex = xSemaphoreCreateMutex();
}

void nadk_device_start() {
  // acquire mutex
  NADK_LOCK(nadk_device_mutex);

  // check if already running
  if (nadk_device_process_started) {
    ESP_LOGE(NADK_LOG_TAG, "nadk_device_start: already started");
    NADK_UNLOCK(nadk_device_mutex);
    return;
  }

  // set flag
  nadk_device_process_started = true;

  // create task
  ESP_LOGI(NADK_LOG_TAG, "nadk_device_start: create task");
  xTaskCreatePinnedToCore(nadk_device_process, "core-device", 8192, NULL, 2, &nadk_device_task, 1);

  // release mutex
  NADK_UNLOCK(nadk_device_mutex);
}

void nadk_device_stop() {
  // acquire mutex
  NADK_LOCK(nadk_device_mutex);

  // check if task is still running
  if (!nadk_device_process_started) {
    NADK_UNLOCK(nadk_device_mutex);
    return;
  }

  // set flag
  nadk_device_process_started = false;

  // remove task
  ESP_LOGI(NADK_LOG_TAG, "nadk_device_stop: deleting task");
  vTaskDelete(nadk_device_task);

  // run terminate callback if present
  if (nadk_device->terminate_fn) {
    nadk_device->terminate_fn();
  }

  // release mutex
  NADK_UNLOCK(nadk_device_mutex);
}

void nadk_device_forward(const char *topic, const char *payload, unsigned int len, nadk_scope_t scope) {
  // acquire mutex
  NADK_LOCK(nadk_device_mutex);

  // check collect
  if (scope == NADK_GLOBAL && strcmp(topic, "nadk/collect") == 0) {
    // send announcement
    nadk_device_send_announcement();

    NADK_UNLOCK(nadk_device_mutex);
    return;
  }

  // check ping
  if (scope == NADK_LOCAL && strcmp(topic, "nadk/ping") == 0) {
    // send heartbeat
    nadk_device_send_heartbeat();

    NADK_UNLOCK(nadk_device_mutex);
    return;
  }

  // check ota
  if (scope == NADK_LOCAL && strcmp(topic, "nadk/ota") == 0) {
    // get update size
    nadk_device_ota_remaining_data = strtoll(payload, NULL, 10);
    ESP_LOGI(NADK_LOG_TAG, "nadk_device_forward: begin update with size %lld", nadk_device_ota_remaining_data);

    // begin update
    nadk_ota_begin((uint16_t)nadk_device_ota_remaining_data);

    // request first chunk
    nadk_device_request_next_chunk();

    NADK_UNLOCK(nadk_device_mutex);
    return;
  }

  // check ota chunk
  if (scope == NADK_LOCAL && strcmp(topic, "nadk/ota/chunk") == 0) {
    // forward chunk
    nadk_ota_forward(payload, (uint16_t)len);
    nadk_device_ota_remaining_data -= len;
    ESP_LOGI(NADK_LOG_TAG, "nadk_device_forward: wrote chunk %lld bytes remaining", nadk_device_ota_remaining_data);

    // request next chunk if remaining data
    if (nadk_device_ota_remaining_data > 0) {
      nadk_device_request_next_chunk();

      NADK_UNLOCK(nadk_device_mutex);
      return;
    }

    // send finished message
    nadk_publish_str("nadk/ota/finished", "", 0, false, NADK_LOCAL);
    ESP_LOGI(NADK_LOG_TAG, "nadk_device_forward: finished update");

    // otherwise finish update
    nadk_ota_finish();

    NADK_UNLOCK(nadk_device_mutex);
    return;
  }

  // call handle callback if present
  if (nadk_device->handle_fn) {
    nadk_device->handle_fn(topic, payload, len, scope);
  }

  // release mutex
  NADK_UNLOCK(nadk_device_mutex);
}
