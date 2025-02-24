/* main.c - Application main entry point */

/*
 * Copyright (c) 2015-2016 Intel Corporation
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <errno.h>
#include <stddef.h>
#include <zephyr/kernel.h>
#include <zephyr/types.h>

#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/conn.h>
#include <zephyr/bluetooth/gatt.h>
#include <zephyr/bluetooth/hci.h>
#include <zephyr/bluetooth/uuid.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/sys/byteorder.h>

#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(ble, LOG_LEVEL_DBG);

/* The devicetree node identifier for the "led0" alias. */
#define LED0_NODE DT_ALIAS(led0)
static const struct gpio_dt_spec led = GPIO_DT_SPEC_GET(LED0_NODE, gpios);

#define STACK_SIZE      1024
#define THREAD_PRIORITY 5

/* Use atomic variable, 2 bits for connection and disconnection state */
static ATOMIC_DEFINE(conn_state, 2U);
#define STATE_CONNECTED    1U
#define STATE_DISCONNECTED 2U

/** @brief RGBLED Service UUID */
#define BT_UUID_RGBLED_SERVICE_VAL BT_UUID_128_ENCODE(0x12345678, 0x1234, 0x5678, 0x1234, 0x56789abcdef0)

// f0debc9a7856 3412 7856 3412 78563412 0106

/** @brief RGBLED Pattern Characteristic UUID */
#define BT_UUID_RGBLED_PATTERN_CHAR_VAL BT_UUID_128_ENCODE(0x12345678, 0x1234, 0x5678, 0x1234, 0x56789abcdef1)

/** @brief RGBLED Pattern Characteristic UUID */
#define BT_UUID_RGBLED_INDICATOR_CHAR_VAL BT_UUID_128_ENCODE(0x12345678, 0x1234, 0x5678, 0x1234, 0x56789abcdef2)

#define BT_UUID_RGBLED_SERVICE        BT_UUID_DECLARE_128(BT_UUID_RGBLED_SERVICE_VAL)
#define BT_UUID_RGBLED_PATTERN_CHAR   BT_UUID_DECLARE_128(BT_UUID_RGBLED_PATTERN_CHAR_VAL)
#define BT_UUID_RGBLED_INDICATOR_CHAR BT_UUID_DECLARE_128(BT_UUID_RGBLED_INDICATOR_CHAR_VAL)

extern const char* bt_uuid_str(const struct bt_uuid* uuid);

static void start_scan(void);

static struct bt_conn* default_conn;

static struct bt_uuid_128 discover_uuid = BT_UUID_INIT_128(0);
static struct bt_uuid_16 discover_uuid_ccc = BT_UUID_INIT_16(0);
static struct bt_gatt_discover_params discover_params;
static struct bt_gatt_subscribe_params subscribe_params;
uint16_t rgbled_pattern_char_handle;
uint16_t rgbled_indicator_char_handle;

uint64_t total_rx_count; /* This value is exposed to test code */

static uint8_t notify_func(
    struct bt_conn* conn,
    struct bt_gatt_subscribe_params* params,
    const void* data,
    uint16_t length)
{
    if (!data)
    {
        LOG_DBG("[UNSUBSCRIBED]\n");
        params->value_handle = 0U;
        return BT_GATT_ITER_STOP;
    }

    LOG_DBG("[NOTIFICATION] data %p length %u\n", data, length);

    total_rx_count++;

    return BT_GATT_ITER_CONTINUE;
}

static uint8_t discover_func(
    struct bt_conn* conn,
    const struct bt_gatt_attr* attr,
    struct bt_gatt_discover_params* params)
{
    int err;

    if (!attr)
    {
        LOG_DBG("Discover complete\n");
        (void)memset(params, 0, sizeof(*params));
        return BT_GATT_ITER_STOP;
    }

    LOG_DBG("[ATTRIBUTE] handle %u\n", attr->handle);

    if (!bt_uuid_cmp(discover_params.uuid, BT_UUID_RGBLED_SERVICE))
    {
        LOG_DBG("Found primary RGBLED service\n");
        memcpy(&discover_uuid, BT_UUID_RGBLED_PATTERN_CHAR, sizeof(discover_uuid));
        discover_params.uuid = &discover_uuid.uuid;
        discover_params.start_handle = attr->handle + 1;
        discover_params.type = BT_GATT_DISCOVER_CHARACTERISTIC;

        err = bt_gatt_discover(conn, &discover_params);
        if (err)
        {
            LOG_DBG("Discover failed (err %d)\n", err);
        }
    }
    else if (!bt_uuid_cmp(discover_params.uuid, BT_UUID_RGBLED_PATTERN_CHAR))
    {
        rgbled_pattern_char_handle = bt_gatt_attr_value_handle(attr);
        LOG_DBG("Found RGBLED pattern characteristic with handle %u\n", rgbled_pattern_char_handle);

        memcpy(&discover_uuid, BT_UUID_RGBLED_INDICATOR_CHAR, sizeof(discover_uuid));
        discover_params.uuid = &discover_uuid.uuid;
        discover_params.start_handle = attr->handle + 1;
        discover_params.type = BT_GATT_DISCOVER_CHARACTERISTIC;

        err = bt_gatt_discover(conn, &discover_params);
        if (err)
        {
            LOG_DBG("Discover failed (err %d)\n", err);
        }
    }
    else if (!bt_uuid_cmp(discover_params.uuid, BT_UUID_RGBLED_INDICATOR_CHAR))
    {
        rgbled_indicator_char_handle = bt_gatt_attr_value_handle(attr);
        LOG_DBG("Found RGBLED indicator characteristic with handle %u\n", rgbled_indicator_char_handle);

        memcpy(&discover_uuid_ccc, BT_UUID_GATT_CCC, sizeof(discover_uuid_ccc));
        discover_params.uuid = &discover_uuid_ccc.uuid;
        discover_params.start_handle = attr->handle + 2;
        discover_params.type = BT_GATT_DISCOVER_DESCRIPTOR;
        subscribe_params.value_handle = bt_gatt_attr_value_handle(attr);

        err = bt_gatt_discover(conn, &discover_params);
        if (err)
        {
            LOG_DBG("Discover failed (err %d)\n", err);
        }
    }
    else
    {
        LOG_DBG("Found CCC notify descriptor\n");
        subscribe_params.notify = notify_func;
        subscribe_params.value = BT_GATT_CCC_NOTIFY;
        subscribe_params.ccc_handle = attr->handle;

        err = bt_gatt_subscribe(conn, &subscribe_params);
        if (err && err != -EALREADY)
        {
            LOG_DBG("Subscribe failed (err %d)\n", err);
        }
        else
        {
            LOG_DBG("[SUBSCRIBED]\n");
        }

        return BT_GATT_ITER_STOP;
    }

    return BT_GATT_ITER_STOP;
}

static bool eir_found(struct bt_data* data, void* user_data)
{
    bt_addr_le_t* addr = user_data;
    int i;
    char dev[BT_ADDR_LE_STR_LEN];
    struct bt_conn_le_create_param* create_param;
    struct bt_le_conn_param* param;
    int err;

    bt_addr_le_to_str(addr, dev, sizeof(dev));
    LOG_DBG("[AD]: from %s type %d data_len %u\n", dev, data->type, data->data_len);

    switch (data->type)
    {
    case BT_DATA_UUID128_ALL:

        if (data->data_len < 16)
        {
            LOG_DBG("Invalid UUID length\n");
            return true;
        }

        struct bt_uuid_128 uuid = BT_UUID_INIT_128(0);
        memcpy(uuid.val, data->data, BT_UUID_SIZE_128);

        LOG_DBG("UUID: %s\n", (char*)bt_uuid_str((struct bt_uuid*)&uuid));

        if (bt_uuid_cmp((struct bt_uuid*)&uuid, BT_UUID_RGBLED_SERVICE))
        {
            LOG_DBG("UUID mismatch\n");
            return true;
        }

        LOG_DBG("Found RGBLED service UUID\n");

        char hex_data[data->data_len * 2 + 1];
        for (i = 0; i < data->data_len; i++)
        {
            sprintf(&hex_data[i * 2], "%02x", data->data[i]);
        }
        hex_data[data->data_len * 2] = '\0';
        LOG_DBG("Data: %s\n", hex_data);

        err = bt_le_scan_stop();
        if (err)
        {
            LOG_DBG("Stop LE scan failed (err %d)\n", err);
            return true;
        }

        LOG_DBG("Creating connection with Coded PHY support\n");
        param = BT_LE_CONN_PARAM_DEFAULT;
        create_param = BT_CONN_LE_CREATE_CONN;
        create_param->options |= BT_CONN_LE_OPT_CODED;
        err = bt_conn_le_create(addr, create_param, param, &default_conn);
        if (err)
        {
            LOG_DBG("Create connection with Coded PHY support failed (err %d)\n", err);

            LOG_DBG("Creating non-Coded PHY connection\n");
            create_param->options &= ~BT_CONN_LE_OPT_CODED;
            err = bt_conn_le_create(addr, create_param, param, &default_conn);
            if (err)
            {
                LOG_DBG("Create connection failed (err %d)\n", err);
                start_scan();
            }
        }

        return false;
    }
    return true;
}

static void device_found(const bt_addr_le_t* addr, int8_t rssi, uint8_t type, struct net_buf_simple* ad)
{
    char dev[BT_ADDR_LE_STR_LEN];

    bt_addr_le_to_str(addr, dev, sizeof(dev));

    /* We're only interested in legacy connectable events or
     * possible extended advertising that are connectable.
     */
    // if (type == BT_GAP_ADV_TYPE_ADV_IND || type == BT_GAP_ADV_TYPE_ADV_DIRECT_IND || type == BT_GAP_ADV_TYPE_EXT_ADV)
    // {
    //     LOG_DBG("[DEVICE]: %s, AD evt type %u, AD data len %u, RSSI %i\n", dev, type, ad->len, rssi);

    //     bt_data_parse(ad, eir_found, (void*)addr);
    // }
    /* Hack for specific device to reduce debug messages */
    if (type == BT_GAP_ADV_TYPE_ADV_IND && !strncmp(dev, "EA:F4:A3:ED:27:1C", 5))
    {
        LOG_DBG("[DEVICE]: %s, AD evt type %u, AD data len %u, RSSI %i\n", dev, type, ad->len, rssi);

        bt_data_parse(ad, eir_found, (void*)addr);
    }
}

static void start_scan(void)
{
    int err;

    /* Use active scanning and disable duplicate filtering to handle any
     * devices that might update their advertising data at runtime. */
    struct bt_le_scan_param scan_param = {
        .type = BT_LE_SCAN_TYPE_ACTIVE,
        .options = BT_LE_SCAN_OPT_CODED,
        .interval = BT_GAP_SCAN_FAST_INTERVAL,
        .window = BT_GAP_SCAN_FAST_WINDOW,
    };

    err = bt_le_scan_start(&scan_param, device_found);
    if (err)
    {
        LOG_DBG("Scanning with Coded PHY support failed (err %d)\n", err);

        LOG_DBG("Scanning without Coded PHY\n");
        scan_param.options &= ~BT_LE_SCAN_OPT_CODED;
        err = bt_le_scan_start(&scan_param, device_found);
        if (err)
        {
            LOG_DBG("Scanning failed to start (err %d)\n", err);
            return;
        }
    }

    LOG_DBG("Scanning successfully started\n");
}

static void connected(struct bt_conn* conn, uint8_t conn_err)
{
    char addr[BT_ADDR_LE_STR_LEN];
    int err;

    (void)atomic_set_bit(conn_state, STATE_CONNECTED);
    bt_addr_le_to_str(bt_conn_get_dst(conn), addr, sizeof(addr));

    if (conn_err)
    {
        LOG_DBG("Failed to connect to %s (%u)\n", addr, conn_err);

        bt_conn_unref(default_conn);
        default_conn = NULL;

        start_scan();
        return;
    }

    LOG_INF("Connected: %s\n", addr);

    total_rx_count = 0U;

    if (conn == default_conn)
    {
        memcpy(&discover_uuid, BT_UUID_RGBLED_SERVICE, sizeof(discover_uuid));
        discover_params.uuid = &discover_uuid.uuid;
        discover_params.func = discover_func;
        discover_params.start_handle = BT_ATT_FIRST_ATTRIBUTE_HANDLE;
        discover_params.end_handle = BT_ATT_LAST_ATTRIBUTE_HANDLE;
        discover_params.type = BT_GATT_DISCOVER_PRIMARY;

        err = bt_gatt_discover(default_conn, &discover_params);
        if (err)
        {
            LOG_DBG("Discover failed(err %d)\n", err);
            return;
        }
    }
    gpio_pin_set_dt(&led, 1);
}

static void disconnected(struct bt_conn* conn, uint8_t reason)
{
    char addr[BT_ADDR_LE_STR_LEN];

    (void)atomic_set_bit(conn_state, STATE_DISCONNECTED);
    rgbled_pattern_char_handle = 0;
    bt_addr_le_to_str(bt_conn_get_dst(conn), addr, sizeof(addr));

    LOG_INF("Disconnected: %s (reason 0x%02x)\n", addr, reason);

    if (default_conn != conn)
    {
        return;
    }

    bt_conn_unref(default_conn);
    default_conn = NULL;

    gpio_pin_set_dt(&led, 0);
    start_scan();
}

BT_CONN_CB_DEFINE(conn_callbacks) = {
    .connected = connected,
    .disconnected = disconnected,
};

static void write_func(struct bt_conn* conn, uint8_t err, struct bt_gatt_write_params* params)
{
    if (err)
    {
        LOG_DBG("[write func] Write failed on handle %d (err %d)\n", params->handle, err);
    }
    else
    {
        LOG_DBG("[write func] Write successful\n");
    }
}

void ble_thread(void)
{
    int err;

    if (!gpio_is_ready_dt(&led))
    {
        LOG_DBG("LED device not ready\n");
        return;
    }

    err = gpio_pin_configure_dt(&led, GPIO_OUTPUT_ACTIVE);
    if (err < 0)
    {
        LOG_DBG("Failed to configure LED pin\n");
        return;
    }

    err = bt_enable(NULL);

    if (err)
    {
        LOG_DBG("Bluetooth init failed (err %d)\n", err);
        return;
    }

    LOG_DBG("Bluetooth initialized\n");

    start_scan();

    return;
}

void rgbled_pattern_next(void)
{
    static uint8_t pattern = 0x0;
    int err;
    uint8_t buf[1];

    if (atomic_test_bit(conn_state, STATE_CONNECTED) && rgbled_pattern_char_handle != 0)
    {
        static struct bt_gatt_write_params write_params;
        buf[0] = pattern;

        write_params.handle = rgbled_pattern_char_handle;
        write_params.offset = 0;
        write_params.data = buf;
        write_params.length = 1;
        write_params.func = write_func;
        LOG_DBG("Writing pattern %d to handle %d\n", pattern, write_params.handle);
        err = bt_gatt_write(default_conn, &write_params);
        if (err)
        {
            LOG_DBG("Write failed for pattern %x (err %d)\n", pattern, err);
        }
        else
        {
            LOG_DBG("Write successful\n");
            pattern++;
            if (pattern > 0x2)
            {
                // Back to first pattern
                pattern = 0x0;
            }
        }
    }
}

void rgbled_left_right_hazard(uint8_t state)
{
    int err;
    uint8_t buf[1];

    if (atomic_test_bit(conn_state, STATE_CONNECTED) && rgbled_indicator_char_handle != 0)
    {
        static struct bt_gatt_write_params write_params;
        buf[0] = state;

        write_params.handle = rgbled_indicator_char_handle;
        write_params.offset = 0;
        write_params.data = buf;
        write_params.length = 1;
        write_params.func = write_func;
        LOG_DBG("Writing left_right %d to handle %d\n", state, write_params.handle);
        err = bt_gatt_write(default_conn, &write_params);
        if (err)
        {
            LOG_DBG("Write failed for left_right %x (err %d)\n", state, err);
        }
        else
        {
            LOG_DBG("Write successful\n");
        }
    }
}
K_THREAD_DEFINE(ble_thread_id, STACK_SIZE, ble_thread, NULL, NULL, NULL, THREAD_PRIORITY, 0, 0);