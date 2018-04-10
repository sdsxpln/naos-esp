#include <esp_log.h>
#include <esp_ota_ops.h>
#include <esp_system.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <freertos/task.h>
#include <nvs_flash.h>
#include <string.h>

#include "ble.h"
#include "manager.h"
#include "mqtt.h"
#include "naos.h"
#include "settings.h"
#include "task.h"
#include "update.h"
#include "utils.h"
#include "wifi.h"

typedef enum {
  NAOS_SYSTEM_EVENT_WIFI_CONNECTED,
  NAOS_SYSTEM_EVENT_WIFI_DISCONNECTED,
  NAOS_SYSTEM_EVENT_MQTT_CONNECTED,
  NAOS_SYSTEM_EVENT_MQTT_DISCONNECTED,
  NAOS_SYSTEM_EVENT_RESTART_WIFI,
  NAOS_SYSTEM_EVENT_RESTART_MQTT
} naos_system_event_t;

QueueHandle_t naos_system_queue;

static naos_status_t naos_system_status;

static const char *naos_system_status_string(naos_status_t status) {
  // default state name
  const char *name = "Unknown";

  // triage status
  switch (status) {
    // handle disconnected state
    case NAOS_DISCONNECTED: {
      name = "Disconnected";
      break;
    }

      // handle connected state
    case NAOS_CONNECTED: {
      name = "Connected";
      break;
    }

      // handle networked state
    case NAOS_NETWORKED: {
      name = "Networked";
      break;
    }
  }

  return name;
}

static void naos_system_set_status(naos_status_t status) {
  // get name
  const char *name = naos_system_status_string(status);
  ESP_LOGI(NAOS_LOG_TAG, "naos_system_set_status: %s", name)

  // change state
  naos_system_status = status;

  // update connection status
  naos_ble_notify(NAOS_BLE_CHAR_CONNECTION_STATUS, (char *)name);

  // notify task
  naos_task_notify(status);
}

static void naos_system_configure_wifi() {
  // get ssid & password
  char *wifi_ssid = naos_settings_read(NAOS_SETTING_WIFI_SSID);
  char *wifi_password = naos_settings_read(NAOS_SETTING_WIFI_PASSWORD);

  // configure wifi
  naos_wifi_configure(wifi_ssid, wifi_password);

  // free strings
  free(wifi_ssid);
  free(wifi_password);
}

static void naos_system_start_mqtt() {
  // get settings
  char *mqtt_host = naos_settings_read(NAOS_SETTING_MQTT_HOST);
  char *mqtt_port = naos_settings_read(NAOS_SETTING_MQTT_PORT);
  char *mqtt_client_id = naos_settings_read(NAOS_SETTING_MQTT_CLIENT_ID);
  char *mqtt_username = naos_settings_read(NAOS_SETTING_MQTT_USERNAME);
  char *mqtt_password = naos_settings_read(NAOS_SETTING_MQTT_PASSWORD);
  char *base_topic = naos_settings_read(NAOS_SETTING_BASE_TOPIC);

  // start mqtt
  naos_mqtt_start(mqtt_host, mqtt_port, mqtt_client_id, mqtt_username, mqtt_password, base_topic);

  // free strings
  free(mqtt_host);
  free(mqtt_port);
  free(mqtt_client_id);
  free(mqtt_username);
  free(mqtt_password);
  free(base_topic);
}

static void naos_system_handle_command(const char *command) {
  // detect command
  bool ping = strcmp(command, "ping") == 0;
  bool restart_mqtt = strcmp(command, "restart-mqtt") == 0;
  bool restart_wifi = strcmp(command, "restart-wifi") == 0;
  bool boot_factory = strcmp(command, "boot-factory") == 0;

  // handle ping
  if (ping) {
    // forward ping to task
    naos_task_ping();
  }

  // handle boot factory
  else if (boot_factory) {
    ESP_ERROR_CHECK(esp_ota_set_boot_partition(
        esp_partition_find_first(ESP_PARTITION_TYPE_APP, ESP_PARTITION_SUBTYPE_APP_FACTORY, NULL)));
    esp_restart();
  }

  // handle wifi restart
  else if (restart_wifi) {
    ESP_LOGI(NAOS_LOG_TAG, "naos_system_handle_command: restart wifi");

    // forward event
    naos_system_event_t evt = NAOS_SYSTEM_EVENT_RESTART_WIFI;
    xQueueSend(naos_system_queue, &evt, 0);
  }

  // handle mqtt restart
  else if (restart_mqtt) {
    ESP_LOGI(NAOS_LOG_TAG, "naos_system_handle_command: restart mqtt");

    // forward event
    naos_system_event_t evt = NAOS_SYSTEM_EVENT_RESTART_MQTT;
    xQueueSend(naos_system_queue, &evt, 0);
  }
}

static char *naos_system_read_callback(naos_ble_char_t ch) {
  switch (ch) {
    case NAOS_BLE_CHAR_WIFI_SSID:
      return naos_settings_read(NAOS_SETTING_WIFI_SSID);
    case NAOS_BLE_CHAR_WIFI_PASSWORD:
      return naos_settings_read(NAOS_SETTING_WIFI_PASSWORD);
    case NAOS_BLE_CHAR_MQTT_HOST:
      return naos_settings_read(NAOS_SETTING_MQTT_HOST);
    case NAOS_BLE_CHAR_MQTT_PORT:
      return naos_settings_read(NAOS_SETTING_MQTT_PORT);
    case NAOS_BLE_CHAR_MQTT_CLIENT_ID:
      return naos_settings_read(NAOS_SETTING_MQTT_CLIENT_ID);
    case NAOS_BLE_CHAR_MQTT_USERNAME:
      return naos_settings_read(NAOS_SETTING_MQTT_USERNAME);
    case NAOS_BLE_CHAR_MQTT_PASSWORD:
      return naos_settings_read(NAOS_SETTING_MQTT_PASSWORD);
    case NAOS_BLE_CHAR_DEVICE_TYPE:
      return strdup(naos_config()->device_type);
    case NAOS_BLE_CHAR_DEVICE_NAME:
      return naos_settings_read(NAOS_SETTING_DEVICE_NAME);
    case NAOS_BLE_CHAR_BASE_TOPIC:
      return naos_settings_read(NAOS_SETTING_BASE_TOPIC);
    case NAOS_BLE_CHAR_CONNECTION_STATUS:
      return strdup(naos_system_status_string(naos_system_status));
    case NAOS_BLE_CHAR_COMMAND:
      return NULL;
  }

  return NULL;
}

static void naos_system_write_callback(naos_ble_char_t ch, const char *value) {
  switch (ch) {
    case NAOS_BLE_CHAR_WIFI_SSID:
      naos_settings_write(NAOS_SETTING_WIFI_SSID, value);
      return;
    case NAOS_BLE_CHAR_WIFI_PASSWORD:
      naos_settings_write(NAOS_SETTING_WIFI_PASSWORD, value);
      return;
    case NAOS_BLE_CHAR_MQTT_HOST:
      naos_settings_write(NAOS_SETTING_MQTT_HOST, value);
      return;
    case NAOS_BLE_CHAR_MQTT_PORT:
      naos_settings_write(NAOS_SETTING_MQTT_PORT, value);
      return;
    case NAOS_BLE_CHAR_MQTT_CLIENT_ID:
      naos_settings_write(NAOS_SETTING_MQTT_CLIENT_ID, value);
      return;
    case NAOS_BLE_CHAR_MQTT_USERNAME:
      naos_settings_write(NAOS_SETTING_MQTT_USERNAME, value);
      return;
    case NAOS_BLE_CHAR_MQTT_PASSWORD:
      naos_settings_write(NAOS_SETTING_MQTT_PASSWORD, value);
      return;
    case NAOS_BLE_CHAR_DEVICE_TYPE:
      return;
    case NAOS_BLE_CHAR_DEVICE_NAME:
      naos_settings_write(NAOS_SETTING_DEVICE_NAME, value);
      return;
    case NAOS_BLE_CHAR_BASE_TOPIC:
      naos_settings_write(NAOS_SETTING_BASE_TOPIC, value);
      return;
    case NAOS_BLE_CHAR_CONNECTION_STATUS:
      return;
    case NAOS_BLE_CHAR_COMMAND:
      naos_system_handle_command(value);
      return;
  }
}

static void naos_system_wifi_callback(naos_wifi_status_t status) {
  switch (status) {
    case NAOS_WIFI_STATUS_CONNECTED: {
      ESP_LOGI(NAOS_LOG_TAG, "naos_system_wifi_callback: connected");

      // forward event
      naos_system_event_t evt = NAOS_SYSTEM_EVENT_WIFI_CONNECTED;
      xQueueSend(naos_system_queue, &evt, 0);

      break;
    }

    case NAOS_WIFI_STATUS_DISCONNECTED: {
      ESP_LOGI(NAOS_LOG_TAG, "naos_system_wifi_callback: disconnected");

      // forward event
      naos_system_event_t evt = NAOS_SYSTEM_EVENT_WIFI_DISCONNECTED;
      xQueueSend(naos_system_queue, &evt, 0);

      break;
    }
  }
}

static void naos_system_mqtt_callback(esp_mqtt_status_t status) {
  switch (status) {
    case ESP_MQTT_STATUS_CONNECTED: {
      ESP_LOGI(NAOS_LOG_TAG, "naos_system_mqtt_callback: connected");

      // forward event
      naos_system_event_t evt = NAOS_SYSTEM_EVENT_MQTT_CONNECTED;
      xQueueSend(naos_system_queue, &evt, 0);

      break;
    }

    case ESP_MQTT_STATUS_DISCONNECTED: {
      ESP_LOGI(NAOS_LOG_TAG, "naos_system_mqtt_callback: disconnected");

      // forward event
      naos_system_event_t evt = NAOS_SYSTEM_EVENT_MQTT_DISCONNECTED;
      xQueueSend(naos_system_queue, &evt, 0);

      break;
    }
  }
}

static void naos_system_task(void *_) {
  // set initial state
  naos_system_set_status(NAOS_DISCONNECTED);

  // initially configure wifi
  naos_system_configure_wifi();

  // loop forever
  for (;;) {
    // receive next event
    naos_system_event_t evt;
    while (xQueueReceive(naos_system_queue, &evt, portMAX_DELAY) == pdTRUE) {
      switch (evt) {
        case NAOS_SYSTEM_EVENT_WIFI_CONNECTED: {
          ESP_LOGI(NAOS_LOG_TAG, "naos_system_task: NAOS_SYSTEM_EVENT_WIFI_CONNECTED");

          // check if connection is new
          if (naos_system_status == NAOS_DISCONNECTED) {
            // change sate
            naos_system_set_status(NAOS_CONNECTED);

            // start mqtt
            naos_system_start_mqtt();
          }

          break;
        }

        case NAOS_SYSTEM_EVENT_WIFI_DISCONNECTED: {
          ESP_LOGI(NAOS_LOG_TAG, "naos_system_task: NAOS_SYSTEM_EVENT_WIFI_DISCONNECTED");

          // check if we have been networked
          if (naos_system_status == NAOS_NETWORKED) {
            // stop task
            naos_task_stop();

            // stop manager
            naos_manager_stop();
          }

          // check if we have been connected
          if (naos_system_status >= NAOS_CONNECTED) {
            // stop mqtt
            naos_mqtt_stop();

            // change state
            naos_system_set_status(NAOS_DISCONNECTED);
          }

          break;
        }

        case NAOS_SYSTEM_EVENT_MQTT_CONNECTED: {
          ESP_LOGI(NAOS_LOG_TAG, "naos_system_task: NAOS_SYSTEM_EVENT_MQTT_CONNECTED");

          // check if connection is new
          if (naos_system_status == NAOS_CONNECTED) {
            // change state
            naos_system_set_status(NAOS_NETWORKED);

            // setup manager
            naos_manager_start();

            // start task
            naos_task_start();
          }

          break;
        }

        case NAOS_SYSTEM_EVENT_MQTT_DISCONNECTED: {
          ESP_LOGI(NAOS_LOG_TAG, "naos_system_task: NAOS_SYSTEM_EVENT_MQTT_DISCONNECTED");

          // change state
          naos_system_set_status(NAOS_CONNECTED);

          // stop task
          naos_task_stop();

          // terminate manager
          naos_manager_stop();

          // restart mqtt
          naos_system_start_mqtt();

          break;
        }

        case NAOS_SYSTEM_EVENT_RESTART_WIFI: {
          ESP_LOGI(NAOS_LOG_TAG, "naos_system_task: NAOS_SYSTEM_EVENT_RESTART_WIFI");

          switch (naos_system_status) {
            case NAOS_NETWORKED: {
              // stop task
              naos_task_stop();

              // stop manager
              naos_manager_stop();

              // fallthrough
            }

            case NAOS_CONNECTED: {
              // stop mqtt client
              naos_mqtt_stop();

              // change state
              naos_system_set_status(NAOS_DISCONNECTED);

              // fallthrough
            }

            case NAOS_DISCONNECTED: {
              // restart wifi
              naos_system_configure_wifi();
            }
          }

          break;
        }

        case NAOS_SYSTEM_EVENT_RESTART_MQTT: {
          ESP_LOGI(NAOS_LOG_TAG, "naos_system_task: NAOS_SYSTEM_EVENT_RESTART_MQTT");

          switch (naos_system_status) {
            case NAOS_NETWORKED: {
              // stop task
              naos_task_stop();

              // stop manager
              naos_manager_stop();

              // change state
              naos_system_set_status(NAOS_CONNECTED);

              // fallthrough
            }

            case NAOS_CONNECTED: {
              // stop mqtt client
              naos_mqtt_stop();

              // restart mqtt
              naos_system_start_mqtt();

              // fallthrough
            }

            case NAOS_DISCONNECTED: {
              // do nothing if not yet connected
            }
          }

          break;
        }
      }
    }
  }
}

void naos_system_init() {
  // delay startup by max 5000ms if set
  if (naos_config()->delay_startup) {
    uint32_t delay = esp_random() / 858994;
    ESP_LOGI(NAOS_LOG_TAG, "naos_system_init: delay startup by %dms", delay);
    naos_delay(delay);
  }

  // create queue
  naos_system_queue = xQueueCreate(16, sizeof(naos_system_event_t));

  // initialize flash memory
  ESP_ERROR_CHECK(nvs_flash_init());

  // init settings
  naos_settings_init();

  // init task
  naos_task_init();

  // init manager
  naos_manager_init();

  // initialize bluetooth stack
  naos_ble_init(naos_system_read_callback, naos_system_write_callback);

  // initialize wifi stack
  naos_wifi_init(naos_system_wifi_callback);

  // initialize mqtt client
  naos_mqtt_init(naos_system_mqtt_callback, naos_manager_handle);

  // initialize OTA
  naos_update_init();

  // create task
  ESP_LOGI(NAOS_LOG_TAG, "naos_system_init: create task");
  xTaskCreatePinnedToCore(naos_system_task, "naos-system", 2048, NULL, 2, NULL, 1);
}
