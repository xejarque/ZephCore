/*
 * SPDX-License-Identifier: MIT
 * ZephCore BLE - Meshtastic client service (CONFIG_ZEPHTACORE_MT_APP)
 *
 * The GATT side of the Meshtastic phone link: ToRadio (write), FromRadio (read)
 * and FromNum (read, notify). The GATT callbacks run on the Bluetooth thread;
 * the Meshtastic session lives on the main thread. Two small queues join them
 * and every callback below only posts an event.
 */

#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Largest ToRadio or FromRadio the link carries. */
#define BLE_MT_FRAME_MAX 320

struct ble_mt_cbs {
	void (*on_to_radio)(void);  /* a ToRadio is waiting in zephcore_ble_mt_recv */
	void (*on_from_read)(void); /* the phone took a FromRadio: there is room for the next */
	void (*on_link_lost)(void); /* the phone went away: end the session */
};

void zephcore_ble_mt_init(const struct ble_mt_cbs *cbs);

/** Next ToRadio written by the phone into dest (BLE_MT_FRAME_MAX). Returns its length, 0 if none. */
size_t zephcore_ble_mt_recv(uint8_t *dest);

/** Hand the phone its next FromRadio. False when the queue toward it is full: wait for on_from_read. */
bool zephcore_ble_mt_offer(const uint8_t *data, uint16_t len);

/** Tell the phone, with FromNum, that a FromRadio is waiting. */
void zephcore_ble_mt_notify(uint32_t from_num);

/** Drop the FromRadio frames still queued. Main thread: it is their only producer. */
void zephcore_ble_mt_flush(void);

/** Called by ZephyrBLE.cpp when the central disconnects. */
void zephcore_ble_mt_disconnected(void);

#ifdef __cplusplus
}
#endif
