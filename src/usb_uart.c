/*
 * Copyright (c) 2019 Intel Corporation
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file
 * @brief Sample echo app for CDC ACM class
 *
 * Sample app for USB CDC ACM class driver. The received data is echoed back
 * to the serial port.
 */

#include <stdio.h>
#include <string.h>
#include <zephyr/device.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/led_strip.h>
#include <zephyr/drivers/spi.h>
#include <zephyr/drivers/uart.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/ring_buffer.h>
#include <zephyr/sys/util.h>
#include <zephyr/usb/usb_device.h>
#include <zephyr/usb/usbd.h>

#define STACK_SIZE      1024
#define THREAD_PRIORITY 5

LOG_MODULE_REGISTER(cdc_acm_echo, LOG_LEVEL_INF);

#define ZEPHYR_USER_NODE DT_PATH(zephyr_user)

#define RING_BUF_SIZE 1024
uint8_t ring_buffer[RING_BUF_SIZE];

struct ring_buf ringbuf;

static bool rx_throttled;

const struct device* const uart_dev = DEVICE_DT_GET_ONE(zephyr_cdc_acm_uart);

void ble_thread(void);

static inline void print_baudrate(const struct device* dev)
{
    uint32_t baudrate;
    int ret;

    ret = uart_line_ctrl_get(dev, UART_LINE_CTRL_BAUD_RATE, &baudrate);
    if (ret)
    {
        LOG_WRN("Failed to get baudrate, ret code %d", ret);
    }
    else
    {
        LOG_INF("Baudrate %u", baudrate);
    }
}

static void interrupt_handler(const struct device* dev, void* user_data)
{
    ARG_UNUSED(user_data);

    while (uart_irq_update(dev) && uart_irq_is_pending(dev))
    {
        if (!rx_throttled && uart_irq_rx_ready(dev))
        {
            int recv_len, rb_len;
            uint8_t buffer[64];
            size_t len = MIN(ring_buf_space_get(&ringbuf), sizeof(buffer));

            if (len == 0)
            {
                /* Throttle because ring buffer is full */
                uart_irq_rx_disable(dev);
                rx_throttled = true;
                continue;
            }

            recv_len = uart_fifo_read(dev, buffer, len);
            if (recv_len < 0)
            {
                LOG_ERR("Failed to read UART FIFO");
                recv_len = 0;
            };

            rb_len = ring_buf_put(&ringbuf, buffer, recv_len);
            if (rb_len < recv_len)
            {
                LOG_ERR("Drop %u bytes", recv_len - rb_len);
            }

            LOG_DBG("tty fifo -> ringbuf %d bytes", rb_len);
            if (rb_len)
            {
                uart_irq_tx_enable(dev);
            }
        }

        if (uart_irq_tx_ready(dev))
        {
            uint8_t buffer[64];
            int rb_len, send_len;

            rb_len = ring_buf_get(&ringbuf, buffer, sizeof(buffer));
            if (!rb_len)
            {
                LOG_DBG("Ring buffer empty, disable TX IRQ");
                uart_irq_tx_disable(dev);
                continue;
            }

            if (rx_throttled)
            {
                uart_irq_rx_enable(dev);
                rx_throttled = false;
            }

            send_len = uart_fifo_fill(dev, buffer, rb_len);
            if (send_len < rb_len)
            {
                LOG_ERR("Drop %d bytes", rb_len - send_len);
            }

            LOG_DBG("ringbuf -> tty fifo %d bytes", send_len);
        }
    }
}

int uart_thread(void)
{
    int ret;

    if (!device_is_ready(uart_dev))
    {
        LOG_ERR("CDC ACM device not ready");
        return 0;
    }

    ret = usb_enable(NULL);

    if (ret != 0)
    {
        LOG_ERR("Failed to enable USB");
        return 0;
    }

    ring_buf_init(&ringbuf, sizeof(ring_buffer), ring_buffer);

    LOG_INF("Wait for DTR");

    while (true)
    {
        uint32_t dtr = 0U;

        uart_line_ctrl_get(uart_dev, UART_LINE_CTRL_DTR, &dtr);
        if (dtr)
        {
            break;
        }
        else
        {
            /* Give CPU resources to low priority threads. */
            k_sleep(K_MSEC(100));
        }
    }

    LOG_INF("DTR set");

    /* They are optional, we use them to test the interrupt endpoint */
    ret = uart_line_ctrl_set(uart_dev, UART_LINE_CTRL_DCD, 1);
    if (ret)
    {
        LOG_WRN("Failed to set DCD, ret code %d", ret);
    }

    ret = uart_line_ctrl_set(uart_dev, UART_LINE_CTRL_DSR, 1);
    if (ret)
    {
        LOG_WRN("Failed to set DSR, ret code %d", ret);
    }

    /* Wait 100ms for the host to do all settings */
    k_msleep(100);

    print_baudrate(uart_dev);

    uart_irq_callback_set(uart_dev, interrupt_handler);

    /* Enable rx interrupts */
    uart_irq_rx_enable(uart_dev);

    return 0;
}

K_THREAD_DEFINE(uart_thread_id, STACK_SIZE, uart_thread, NULL, NULL, NULL, THREAD_PRIORITY, 0, 0);
