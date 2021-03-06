#ifndef _NAOS_MANAGER_H
#define _NAOS_MANAGER_H

#include <naos.h>

/**
 * Initialize the manager subsystem.
 *
 * Note: Should only be called once on boot.
 */
void naos_manager_init();

/**
 * Start the manager process.
 */
void naos_manager_start();

/**
 * Handle an incoming message.
 *
 * The message is forwarded to the task if not handled by the manager.
 *
 * @param topic - The topic.
 * @param payload - The payload.
 * @param len - The payload length.
 * @param scope - The scope.
 */
void naos_manager_handle(const char* topic, uint8_t* payload, size_t len, naos_scope_t scope);

/**
 * Stop the manager process.
 */
void naos_manager_stop();

#endif  // _NAOS_MANAGER_H
