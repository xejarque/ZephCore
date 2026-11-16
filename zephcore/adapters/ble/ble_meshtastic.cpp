/*
 * SPDX-License-Identifier: MIT
 * ZephCore BLE - Meshtastic client service. See ble_meshtastic.h.
 *
 * The service symbol must sort after secure_nus_svc and secure_nus_svc_dfu:
 * services register in symbol order and an earlier one shifts the handles.
 */

#include <zephyr/kernel.h>
#include <zephyr/sys/util.h>
#include <zephyr/sys/byteorder.h>
#include <zephyr/logging/log.h>
LOG_MODULE_DECLARE(zephcore_ble, CONFIG_ZEPHCORE_BLE_LOG_LEVEL);

#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/conn.h>
#include <zephyr/bluetooth/gatt.h>
#include <zephyr/bluetooth/uuid.h>
#include <string.h>

#include "ble_meshtastic.h"

#define MT_QUEUE_DEPTH 2

struct mt_frame {
	uint16_t len;
	uint8_t buf[BLE_MT_FRAME_MAX];
};

static struct bt_uuid_128 mt_svc_uuid = BT_UUID_INIT_128(
	BT_UUID_128_ENCODE(0x6ba1b218, 0x15a8, 0x461f, 0x9fa8, 0x5dcae273eafd));
static struct bt_uuid_128 mt_toradio_uuid = BT_UUID_INIT_128(
	BT_UUID_128_ENCODE(0xf75c76d2, 0x129e, 0x4dad, 0xa1dd, 0x7866124401e7));
static struct bt_uuid_128 mt_fromradio_uuid = BT_UUID_INIT_128(
	BT_UUID_128_ENCODE(0x2c55e69e, 0x4993, 0x11ed, 0xb878, 0x0242ac120002));
static struct bt_uuid_128 mt_fromnum_uuid = BT_UUID_INIT_128(
	BT_UUID_128_ENCODE(0xed9da18c, 0xa800, 0x4f66, 0xa670, 0xaa7547e34453));

K_MSGQ_DEFINE(mt_to_queue, sizeof(struct mt_frame), MT_QUEUE_DEPTH, 4);
K_MSGQ_DEFINE(mt_from_queue, sizeof(struct mt_frame), MT_QUEUE_DEPTH, 4);

static const struct ble_mt_cbs *mt_cbs;
static const struct bt_gatt_attr *mt_fromnum_attr;
static atomic_t mt_from_num;

/* The FromRadio being read: a long read comes back with offset > 0 for the
 * same message, so it is popped only at offset 0. Bluetooth thread only. */
static struct mt_frame mt_cur;

static ssize_t mt_toradio_write(struct bt_conn *conn, const struct bt_gatt_attr *attr,
				const void *buf, uint16_t len, uint16_t offset, uint8_t flags)
{
	ARG_UNUSED(conn);
	ARG_UNUSED(attr);
	ARG_UNUSED(flags);

	if (offset != 0 || len == 0 || len > BLE_MT_FRAME_MAX) {
		return BT_GATT_ERR(BT_ATT_ERR_INVALID_ATTRIBUTE_LEN);
	}

	static struct mt_frame f;  /* Bluetooth thread only: keeps the stack small */

	printk("MTDBG write len=%u b0=%02x\n", len, ((const uint8_t *)buf)[0]);
	f.len = len;
	memcpy(f.buf, buf, len);
	if (k_msgq_put(&mt_to_queue, &f, K_NO_WAIT) != 0) {
		LOG_WRN("MT ToRadio queue full");
		return BT_GATT_ERR(BT_ATT_ERR_PREPARE_QUEUE_FULL);
	}
	if (mt_cbs && mt_cbs->on_to_radio) {
		mt_cbs->on_to_radio();
	}
	return len;
}

static ssize_t mt_fromradio_read(struct bt_conn *conn, const struct bt_gatt_attr *attr,
				 void *buf, uint16_t len, uint16_t offset)
{
	if (offset == 0) {
		if (k_msgq_get(&mt_from_queue, &mt_cur, K_NO_WAIT) != 0) {
			mt_cur.len = 0;
		} else if (mt_cbs && mt_cbs->on_from_read) {
			mt_cbs->on_from_read();
		}
	}
	printk("MTDBG read off=%u cur=%u tag=%02x\n", offset, mt_cur.len, mt_cur.len ? mt_cur.buf[0] : 0);
	return bt_gatt_attr_read(conn, attr, buf, len, offset, mt_cur.buf, mt_cur.len);
}

static ssize_t mt_fromnum_read(struct bt_conn *conn, const struct bt_gatt_attr *attr,
			       void *buf, uint16_t len, uint16_t offset)
{
	uint32_t v = sys_cpu_to_le32((uint32_t)atomic_get(&mt_from_num));

	return bt_gatt_attr_read(conn, attr, buf, len, offset, &v, sizeof(v));
}

static void mt_fromnum_ccc_changed(const struct bt_gatt_attr *attr, uint16_t value)
{
	ARG_UNUSED(attr);
	LOG_DBG("MT FromNum notify %s", value == BT_GATT_CCC_NOTIFY ? "on" : "off");
}

BT_GATT_SERVICE_DEFINE(secure_nus_svc_mt,
	BT_GATT_PRIMARY_SERVICE(&mt_svc_uuid),
	BT_GATT_CHARACTERISTIC(&mt_toradio_uuid.uuid,
		BT_GATT_CHRC_WRITE,
		BT_GATT_PERM_WRITE_AUTHEN,
		NULL, mt_toradio_write, NULL),
	BT_GATT_CHARACTERISTIC(&mt_fromradio_uuid.uuid,
		BT_GATT_CHRC_READ,
		BT_GATT_PERM_READ_AUTHEN,
		mt_fromradio_read, NULL, NULL),
	BT_GATT_CHARACTERISTIC(&mt_fromnum_uuid.uuid,
		BT_GATT_CHRC_READ | BT_GATT_CHRC_NOTIFY,
		BT_GATT_PERM_READ_AUTHEN,
		mt_fromnum_read, NULL, NULL),
	BT_GATT_CCC(mt_fromnum_ccc_changed,
		BT_GATT_PERM_READ_AUTHEN | BT_GATT_PERM_WRITE_AUTHEN),
);

void zephcore_ble_mt_init(const struct ble_mt_cbs *cbs)
{
	mt_cbs = cbs;
	mt_fromnum_attr = bt_gatt_find_by_uuid(secure_nus_svc_mt.attrs, secure_nus_svc_mt.attr_count,
					       &mt_fromnum_uuid.uuid);
}

size_t zephcore_ble_mt_recv(uint8_t *dest)
{
	struct mt_frame f;

	if (k_msgq_get(&mt_to_queue, &f, K_NO_WAIT) != 0) {
		return 0;
	}
	memcpy(dest, f.buf, f.len);
	return f.len;
}

bool zephcore_ble_mt_offer(const uint8_t *data, uint16_t len)
{
	struct mt_frame f;

	if (len > BLE_MT_FRAME_MAX) {
		return true;  /* cannot be sent: drop it rather than block the queue */
	}
	f.len = len;
	memcpy(f.buf, data, len);
	return k_msgq_put(&mt_from_queue, &f, K_NO_WAIT) == 0;
}

void zephcore_ble_mt_notify(uint32_t from_num)
{
	uint32_t v = sys_cpu_to_le32(from_num);

	printk("MTDBG notify %u\n", from_num);
	atomic_set(&mt_from_num, (atomic_val_t)from_num);
	if (mt_fromnum_attr) {
		(void)bt_gatt_notify(NULL, mt_fromnum_attr, &v, sizeof(v));
	}
}

void zephcore_ble_mt_flush(void)
{
	k_msgq_purge(&mt_from_queue);
}

void zephcore_ble_mt_disconnected(void)
{
	k_msgq_purge(&mt_to_queue);
	mt_cur.len = 0;
	if (mt_cbs && mt_cbs->on_link_lost) {
		mt_cbs->on_link_lost();
	}
}
