#ifndef NAOS_H
#define NAOS_H

#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>

/**
 * The messages scopes.
 *
 * The 'local' scope denotes messages that are transferred under the configured base topic of the device while the
 * 'global' scope denotes messages that are transferred directly below the root.
 */
typedef enum { NAOS_LOCAL, NAOS_GLOBAL } naos_scope_t;

/**
 * Get the string representation of the specified scope.
 *
 * @param scope - The scope.
 * @return The string value.
 */
const char *naos_scope_str(naos_scope_t scope);

/**
 * The system statuses.
 */
typedef enum { NAOS_DISCONNECTED, NAOS_CONNECTED, NAOS_NETWORKED } naos_status_t;

/**
 * Get the string representation of the specified status.
 *
 * @param scope - The status.
 * @return The string value.
 */
const char *naos_status_str(naos_status_t status);

/**
 * The main configuration object.
 */
typedef struct {
  /**
   * The device type.
   */
  const char *device_type;

  /**
   * The firmware version.
   */
  const char *firmware_version;

  /**
   * The callback that is called once the device comes online.
   */
  void (*online_callback)();

  /**
   * The callback that is called when a parameter has been updated.
   *
   * @param param - The parameter.
   * @param value - The value.
   */
  void (*update_callback)(const char *param, const char *value);

  /**
   * The message callback is called with incoming messages.
   *
   * Note: The base topic has already been removed from the topic and should not start with a '/'.
   *
   * @param topic
   * @param payload
   * @param len
   * @param scope
   */
  void (*message_callback)(const char *topic, const char *payload, unsigned int len, naos_scope_t scope);

  /**
   * The loop callback is called in over and over as long as the device is online.
   */
  void (*loop_callback)();

  /**
   * The interval of the loop callback in milliseconds.
   */
  int loop_interval;

  /**
   * The offline callback is called once the device becomes offline.
   */
  void (*offline_callback)();

  /**
   * The callback is called once the device has changed its status.
   */
  void (*status_callback)(naos_status_t status);

  /**
   * If set, the device will randomly (up to 5s) delay startup to overcome WiFi and MQTT congestion issues if many
   * devices restart at the same time.
   */
  bool delay_startup;
} naos_config_t;

/**
 * Initialize the system.
 *
 * Note: Should only be called once on boot.
 *
 * @param config - The configuration object.
 */
void naos_init(naos_config_t *config);

/**
 * Write a log message.
 *
 * The message will be printed in the console and published to the broker if the device has logging enabled.
 *
 * @param fmt - The message format.
 * @param ... - The used arguments.
 */
void naos_log(const char *fmt, ...);

/**
 * Will return the value of the requested parameter.
 *
 * Note: The returned pointer is valid until the next call to naos_get().
 *
 * @param param - The parameter.
 * @return Pointer to value.
 */
char *naos_get(const char *param);

/**
 * Subscribe to specified topic.
 *
 * The topic is automatically prefixed with the configured base topic if the scope is local.
 *
 * @param topic
 * @param scope
 * @return
 */
bool naos_subscribe(const char *topic, int qos, naos_scope_t scope);

/**
 * Unsubscribe from specified topic.
 *
 * The topic is automatically prefixed with the configured base topic if the scope is local.
 *
 * @param topic
 * @param scope
 * @return
 */
bool naos_unsubscribe(const char *topic, naos_scope_t scope);

/**
 * Publish bytes payload to specified topic.
 *
 * The topic is automatically prefixed with the configured base topic if the scope is local.
 *
 * @param topic
 * @param payload
 * @param len
 * @param qos
 * @param retained
 * @param scope
 * @return
 */
bool naos_publish(const char *topic, void *payload, uint16_t len, int qos, bool retained, naos_scope_t scope);

/**
 * Publish string to specified topic.
 *
 * The topic is automatically prefixed with the configured base topic if the scope is local.
 *
 * @param topic
 * @param str
 * @param qos
 * @param retained
 * @param scope
 * @return
 */
bool naos_publish_str(const char *topic, const char *str, int qos, bool retained, naos_scope_t scope);

/**
 * Publish integer to specified topic.
 *
 * The topic is automatically prefixed with the configured base topic if the scope is local.
 *
 * @param topic
 * @param num
 * @param qos
 * @param retained
 * @param scope
 * @return
 */
bool naos_publish_int(const char *topic, int num, int qos, bool retained, naos_scope_t scope);

#endif  // NAOS_H