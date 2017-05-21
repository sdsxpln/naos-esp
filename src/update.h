#ifndef _NADK_UPDATE_H
#define _NADK_UPDATE_H

#include <stdint.h>

/**
 * Initialize the firmware update subsystem.
 *
 * Note: Should only be called once on boot.
 */
void nadk_update_init();

/**
 * Begin a new firmware update.
 *
 * @param size - Total size of the update.
 */
void nadk_update_begin(uint16_t size);

/**
 * Write an incoming chunk of the new firmware image.
 *
 * @param chunk - The data chunk.
 * @param len - Length of the data chunk.
 */
void nadk_update_write(const char *chunk, uint16_t len);

/**
 * Finish the firmware update.
 */
void nadk_update_finish();

#endif  // _NADK_UPDATE_H
