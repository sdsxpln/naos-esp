#ifndef _NADK_MANAGER_H
#define _NADK_MANAGER_H

#include <stdbool.h>

#include <nadk.h>

/**
 * Initialize the manager subsystem.
 *
 * Note: Should only be called once on boot.
 */
void nadk_manager_init();

/**
 * Start the manager process.
 */
void nadk_manager_start();

/**
 * Allow manager to handle an incoming message. Will return true if the message has been handled.
 *
 * @param topic - The topic.
 * @param payload - The payload.
 * @param len - The payload length.
 * @param scope - The scope.
 */
bool nadk_manager_handle(const char* topic, const char* payload, unsigned int len, nadk_scope_t scope);

/**
 * Stop the manager process.
 */
void nadk_manager_stop();

#endif  // _NADK_MANAGER_H