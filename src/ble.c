/*
 * Copyright (c) 2024-2025 Robert Wessels
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
static ATOMIC_DEFINE(conn_state, 4U);
#define STATE_CONNECTED               1U
#define STATE_DISCONNECTED            2U
#define STATE_PERIPHERAL_CONNECTED    3U
#define STATE_PERIPHERAL_DISCONNECTED 4U

/** @brief RGBLED Service UUID */
#define BT_UUID_RGBLED_CTRL_SERVICE_VAL BT_UUID_128_ENCODE(0x12345678, 0x1234, 0x5678, 0x1234, 0x56789abcdef4)
#define BT_UUID_RGBLED_SERVICE_VAL      BT_UUID_128_ENCODE(0x12345678, 0x1234, 0x5678, 0x1234, 0x56789abcdef0)

// f0debc9a7856 3412 7856 3412 78563412 0106

/** @brief RGBLED Pattern Characteristic UUID */
#define BT_UUID_RGBLED_PATTERN_CHAR_VAL BT_UUID_128_ENCODE(0x12345678, 0x1234, 0x5678, 0x1234, 0x56789abcdef1)

/** @brief RGBLED Pattern Characteristic UUID */
#define BT_UUID_RGBLED_INDICATOR_CHAR_VAL BT_UUID_128_ENCODE(0x12345678, 0x1234, 0x5678, 0x1234, 0x56789abcdef2)

#define BT_UUID_RGBLED_SERVICE        BT_UUID_DECLARE_128(BT_UUID_RGBLED_SERVICE_VAL)
#define BT_UUID_RGBLED_PATTERN_CHAR   BT_UUID_DECLARE_128(BT_UUID_RGBLED_PATTERN_CHAR_VAL)
#define BT_UUID_RGBLED_INDICATOR_CHAR BT_UUID_DECLARE_128(BT_UUID_RGBLED_INDICATOR_CHAR_VAL)

static const struct bt_data ad[] = {
    BT_DATA_BYTES(BT_DATA_FLAGS, (BT_LE_AD_GENERAL | BT_LE_AD_NO_BREDR)),
    BT_DATA_BYTES(BT_DATA_UUID128_ALL, BT_UUID_RGBLED_CTRL_SERVICE_VAL),
};

#if !defined(CONFIG_BT_EXT_ADV)
static const struct bt_data sd[] = {
    BT_DATA_BYTES(BT_DATA_UUID128_ALL, BT_UUID_RGBLED_CTRL_SERVICE_VAL),
    BT_DATA(BT_DATA_NAME_COMPLETE, CONFIG_BT_DEVICE_NAME, sizeof(CONFIG_BT_DEVICE_NAME) - 1),
};
#endif /* !CONFIG_BT_EXT_ADV */

static enum {
    BLE_CENTRAL_DISCONNECTED,
    BLE_PERIPHERAL_DISCONNECTED,
    BLE_START_SCAN,
    BLE_START_ADVERTISING,
} ble_state;

static struct k_work_delayable ble_work;

extern const char* bt_uuid_str(const struct bt_uuid* uuid);
static void write_func(struct bt_conn* conn, uint8_t err, struct bt_gatt_write_params* params);
static void start_scan(void);
static void start_advertising(void);

static struct bt_conn* default_conn;

static struct bt_uuid_128 discover_uuid = BT_UUID_INIT_128(0);
static struct bt_uuid_16 discover_uuid_ccc = BT_UUID_INIT_16(0);
static struct bt_gatt_discover_params discover_params;
static struct bt_gatt_subscribe_params subscribe_params;
uint16_t rgbled_pattern_char_handle;
uint16_t rgbled_indicator_char_handle;

uint64_t total_rx_count; /* This value is exposed to test code */

typedef void (*bt_connected_cb_t)(void);
static bt_connected_cb_t connected_cb;

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
        LOG_DBG("Writing pattern %d to handle %d", pattern, write_params.handle);
        err = bt_gatt_write(default_conn, &write_params);
        if (err)
        {
            LOG_DBG("Write failed for pattern %x (err %d)", pattern, err);
        }
        else
        {
            LOG_DBG("Write successful");
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
        LOG_DBG("Writing left_right %d to handle %d", state, write_params.handle);
        err = bt_gatt_write(default_conn, &write_params);
        if (err)
        {
            LOG_DBG("Write failed for left_right %x (err %d)", state, err);
        }
        else
        {
            LOG_DBG("Write successful");
        }
    }
}

extern void ble_on_connected(void (*connected)(void))
{
    connected_cb = connected;
}

static void ble_timeout(struct k_work* work)
{
    LOG_DBG("BLE timeout");
    switch (ble_state)
    {
    case BLE_PERIPHERAL_DISCONNECTED:
        ble_state = BLE_START_ADVERTISING;
        __fallthrough; // silence gcc warning about missing break
    case BLE_START_ADVERTISING:
        start_advertising();
        break;
    case BLE_CENTRAL_DISCONNECTED:
        ble_state = BLE_START_SCAN;
        __fallthrough;
    case BLE_START_SCAN:
        start_scan();
        break;
    default:
        break;
    }
}

static uint8_t notify_func(
    struct bt_conn* conn,
    struct bt_gatt_subscribe_params* params,
    const void* data,
    uint16_t length)
{
    if (!data)
    {
        LOG_DBG("[UNSUBSCRIBED]");
        params->value_handle = 0U;
        return BT_GATT_ITER_STOP;
    }

    LOG_DBG("[NOTIFICATION] data %p length %u", data, length);

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
        LOG_DBG("Discover complete");
        (void)memset(params, 0, sizeof(*params));
        return BT_GATT_ITER_STOP;
    }

    LOG_DBG("[ATTRIBUTE] handle %u", attr->handle);

    if (!bt_uuid_cmp(discover_params.uuid, BT_UUID_RGBLED_SERVICE))
    {
        LOG_DBG("Found primary RGBLED service");
        memcpy(&discover_uuid, BT_UUID_RGBLED_PATTERN_CHAR, sizeof(discover_uuid));
        discover_params.uuid = &discover_uuid.uuid;
        discover_params.start_handle = attr->handle + 1;
        discover_params.type = BT_GATT_DISCOVER_CHARACTERISTIC;

        err = bt_gatt_discover(conn, &discover_params);
        if (err)
        {
            LOG_DBG("Discover failed (err %d)", err);
        }
    }
    else if (!bt_uuid_cmp(discover_params.uuid, BT_UUID_RGBLED_PATTERN_CHAR))
    {
        rgbled_pattern_char_handle = bt_gatt_attr_value_handle(attr);
        LOG_DBG("Found RGBLED pattern characteristic with handle %u", rgbled_pattern_char_handle);

        memcpy(&discover_uuid, BT_UUID_RGBLED_INDICATOR_CHAR, sizeof(discover_uuid));
        discover_params.uuid = &discover_uuid.uuid;
        discover_params.start_handle = attr->handle + 1;
        discover_params.type = BT_GATT_DISCOVER_CHARACTERISTIC;

        err = bt_gatt_discover(conn, &discover_params);
        if (err)
        {
            LOG_DBG("Discover failed (err %d)", err);
        }
    }
    else if (!bt_uuid_cmp(discover_params.uuid, BT_UUID_RGBLED_INDICATOR_CHAR))
    {
        rgbled_indicator_char_handle = bt_gatt_attr_value_handle(attr);
        LOG_DBG("Found RGBLED indicator characteristic with handle %u", rgbled_indicator_char_handle);

        memcpy(&discover_uuid_ccc, BT_UUID_GATT_CCC, sizeof(discover_uuid_ccc));
        discover_params.uuid = &discover_uuid_ccc.uuid;
        discover_params.start_handle = attr->handle + 2;
        discover_params.type = BT_GATT_DISCOVER_DESCRIPTOR;
        subscribe_params.value_handle = bt_gatt_attr_value_handle(attr);

        err = bt_gatt_discover(conn, &discover_params);
        if (err)
        {
            LOG_DBG("Discover failed (err %d)", err);
        }
    }
    else
    {
        LOG_DBG("Found CCC notify descriptor");
        subscribe_params.notify = notify_func;
        subscribe_params.value = BT_GATT_CCC_NOTIFY;
        subscribe_params.ccc_handle = attr->handle;

        err = bt_gatt_subscribe(conn, &subscribe_params);
        if (err && err != -EALREADY)
        {
            LOG_DBG("Subscribe failed (err %d)", err);
        }
        else
        {
            LOG_DBG("[SUBSCRIBED]");
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
    LOG_DBG("[AD]: from %s type %d data_len %u", dev, data->type, data->data_len);

    switch (data->type)
    {
    case BT_DATA_UUID128_ALL:

        err = bt_le_scan_stop();
        if (err)
        {
            LOG_DBG("Stop LE scan failed (err %d)", err);
            return true;
        }

        if (data->data_len < 16)
        {
            LOG_DBG("Invalid UUID length");
            return true;
        }

        struct bt_uuid_128 uuid = BT_UUID_INIT_128(0);
        memcpy(uuid.val, data->data, BT_UUID_SIZE_128);

        LOG_DBG("UUID: %s", (char*)bt_uuid_str((struct bt_uuid*)&uuid));

        if (bt_uuid_cmp((struct bt_uuid*)&uuid, BT_UUID_RGBLED_SERVICE))
        {
            LOG_DBG("UUID mismatch");
            return true;
        }

        LOG_DBG("Found RGBLED service UUID");

        char hex_data[data->data_len * 2 + 1];
        for (i = 0; i < data->data_len; i++)
        {
            sprintf(&hex_data[i * 2], "%02x", data->data[i]);
        }
        hex_data[data->data_len * 2] = '\0';
        LOG_DBG("Data: %s", hex_data);

        LOG_DBG("Creating connection with Coded PHY support");
        param = BT_LE_CONN_PARAM_DEFAULT;
        create_param = BT_CONN_LE_CREATE_CONN;
        create_param->options |= BT_CONN_LE_OPT_CODED;
        err = bt_conn_le_create(addr, create_param, param, &default_conn);

        if (err)
        {
            LOG_DBG("Create connection with Coded PHY support failed (err %d)", err);

            LOG_DBG("Creating non-Coded PHY connection");
            create_param->options &= ~BT_CONN_LE_OPT_CODED;
            err = bt_conn_le_create(addr, create_param, param, &default_conn);
            if (err)
            {
                LOG_DBG("Create connection failed (err %d)", err);
                ble_state = BLE_CENTRAL_DISCONNECTED;
                k_work_reschedule(&ble_work, K_NO_WAIT);
                return false;
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

    /* Hack for specific device to reduce debug messages */
    if (type == BT_GAP_ADV_TYPE_ADV_IND && !strncmp(dev, "EA:F4:A3:ED:27:1C", 5))
    {
        LOG_DBG("[DEVICE]: %s, AD evt type %u, AD data len %u, RSSI %i", dev, type, ad->len, rssi);

        bt_data_parse(ad, eir_found, (void*)addr);
    }
}

static void start_advertising(void)
{
    int err;

    err = bt_le_adv_start(BT_LE_ADV_CONN_ONE_TIME, ad, ARRAY_SIZE(ad), sd, ARRAY_SIZE(sd));
    if (err)
    {
        LOG_DBG("Advertising failed to start (err %d)", err);
        return;
    }

    LOG_DBG("Advertising successfully started");
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
        LOG_DBG("Scanning with Coded PHY support failed (err %d)", err);

        LOG_DBG("Scanning without Coded PHY");
        scan_param.options &= ~BT_LE_SCAN_OPT_CODED;
        err = bt_le_scan_start(&scan_param, device_found);
        if (err)
        {
            LOG_DBG("Scanning failed to start (err %d)", err);
            return;
        }
    }

    LOG_DBG("Scanning successfully started");
}

static void connected(struct bt_conn* conn, uint8_t conn_err)
{
    char addr[BT_ADDR_LE_STR_LEN];
    int err;
    struct bt_conn_info info;

    bt_conn_get_info(conn, &info);
    bt_addr_le_to_str(bt_conn_get_dst(conn), addr, sizeof(addr));

    if (conn_err)
    {
        LOG_DBG("Failed to connect to %s (%u)", addr, conn_err);

        switch (info.role)
        {
        case BT_CONN_ROLE_CENTRAL:
            bt_conn_unref(default_conn);
            default_conn = NULL;
            start_scan();
            break;

        case BT_CONN_ROLE_PERIPHERAL:

            break;

        default:
            LOG_DBG("Unknown connection role");
            break;
        }
        return;
    }

    if (info.role == BT_CONN_ROLE_PERIPHERAL)
    {
        /* Nothing to do for now */
        LOG_DBG("Connection type slave");
        return;
    }

    /* Only valid connection left is CENTRAL. If not, reject */
    if (info.role != BT_CONN_ROLE_CENTRAL)
    {
        LOG_DBG("Connection type unknown");
        return;
    }

    // bt_le_adv_stop();
    (void)atomic_set_bit(conn_state, STATE_CONNECTED);
    LOG_INF("Connected: %s", addr);

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
        LOG_DBG("Discovering services");
        if (err)
        {
            LOG_DBG("Discover failed(err %d)", err);
            return;
        }
    }

    /* Turn on connection LED */
    gpio_pin_set_dt(&led, 1);
}

static void disconnected(struct bt_conn* conn, uint8_t reason)
{
    int err;
    struct bt_conn_info info;
    char addr[BT_ADDR_LE_STR_LEN];

    bt_addr_le_to_str(bt_conn_get_dst(conn), addr, sizeof(addr));
    bt_conn_get_info(conn, &info);

    LOG_INF("Disconnected: %s (reason 0x%02x)", addr, reason);

    if (info.role == BT_CONN_ROLE_PERIPHERAL)
    {
        ble_state = BLE_PERIPHERAL_DISCONNECTED;
        k_work_reschedule(&ble_work, K_NO_WAIT);
        return;
    }

    if (info.role != BT_CONN_ROLE_CENTRAL)
    {
        LOG_DBG("Connection type unknown");
        return;
    }

    (void)atomic_set_bit(conn_state, STATE_DISCONNECTED);

    rgbled_pattern_char_handle = 0;

    LOG_INF("Disconnected: %s (reason 0x%02x)", addr, reason);

    if (default_conn != conn)
    {
        return;
    }

    bt_conn_unref(default_conn);
    default_conn = NULL;

    LOG_DBG("Starting scan and advertising");
    start_scan();
    /* Turn off Connection LED */
    gpio_pin_set_dt(&led, 0);
}

BT_CONN_CB_DEFINE(conn_callbacks) = {
    .connected = connected,
    .disconnected = disconnected,
};

static void write_func(struct bt_conn* conn, uint8_t err, struct bt_gatt_write_params* params)
{
    if (err)
    {
        LOG_DBG("[write func] Write failed on handle %d (err %d)", params->handle, err);
    }
    else
    {
        LOG_DBG("[write func] Write successful");
    }
}

static void bt_ready(int err)
{
    if (err)
    {
        LOG_DBG("Bluetooth init failed (err %d)", err);
        return;
    }

    LOG_DBG("Bluetooth initialized");

    start_advertising();
    ble_state = BLE_START_SCAN;
    k_work_reschedule(&ble_work, K_NO_WAIT);
}

void ble_thread(void)
{
    int err;

    if (!gpio_is_ready_dt(&led))
    {
        LOG_DBG("LED device not ready");
        return;
    }

    err = gpio_pin_configure_dt(&led, GPIO_OUTPUT_INACTIVE);
    if (err < 0)
    {
        LOG_DBG("Failed to configure LED pin");
        return;
    }

    k_work_init_delayable(&ble_work, ble_timeout);

    err = bt_enable(bt_ready);

    if (err)
    {
        LOG_DBG("Bluetooth init failed (err %d)", err);
        return;
    }

    while (1)
    {
        k_sleep(K_SECONDS(3));
    }
    return;
}

K_THREAD_DEFINE(ble_thread_id, STACK_SIZE, ble_thread, NULL, NULL, NULL, THREAD_PRIORITY, 0, 0);
