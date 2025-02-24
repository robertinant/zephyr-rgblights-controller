#include "button.h"
#include <zephyr/drivers/gpio.h>
#include <zephyr/kernel.h>

#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(button, LOG_LEVEL_INF);

#pragma GCC push_options
#pragma GCC optimize("O0")

#pragma GCC pop_options

#define MAX_BUTTONS 4

struct button_data
{
    struct gpio_dt_spec spec;
    struct gpio_callback cb_data;
    button_event_handler_t user_cb;
    struct k_work_delayable cooldown_work;
    uint32_t code;
};

static struct button_data buttons[MAX_BUTTONS];

static void cooldown_expired(struct k_work* work)
{
    struct k_work_delayable* dwork = k_work_delayable_from_work(work);
    struct button_data* button = CONTAINER_OF(dwork, struct button_data, cooldown_work);
    int val = gpio_pin_get_dt(&button->spec);
    enum button_evt evt = val ? BUTTON_EVT_PRESSED : BUTTON_EVT_RELEASED;
    LOG_DBG("Button %d %s\n", button->spec.pin, val ? "pressed" : "released");

    if (button->user_cb)
    {
        button->user_cb(evt, button->code);
    }
}

void button_pressed(const struct device* dev, struct gpio_callback* cb, uint32_t pins)
{
    LOG_DBG("Button pressed\n");
    struct button_data* button = CONTAINER_OF(cb, struct button_data, cb_data);
    k_work_reschedule(&button->cooldown_work, K_MSEC(15));
}

int button_init(int index, const struct gpio_dt_spec* spec, uint32_t code, button_event_handler_t handler)
{

    if (index < 0 || index >= MAX_BUTTONS || !handler || !spec)
    {
        return -EINVAL;
    }

    LOG_DBG("Initializing button %d\n", index);

    struct button_data* button = &buttons[index];
    button->spec = *spec;
    button->user_cb = handler;
    button->code = code;

    if (!device_is_ready(button->spec.port))
    {
        return -EIO;
    }

    int err = gpio_pin_configure_dt(&button->spec, GPIO_INPUT);
    if (err)
    {
        return err;
    }

    err = gpio_pin_interrupt_configure_dt(&button->spec, GPIO_INT_EDGE_BOTH);
    if (err)
    {
        return err;
    }

    gpio_init_callback(&button->cb_data, button_pressed, BIT(button->spec.pin));
    err = gpio_add_callback(button->spec.port, &button->cb_data);
    if (err)
    {
        return err;
    }

    k_work_init_delayable(&button->cooldown_work, cooldown_expired);

    return 0;
}

void buttons_init(button_event_handler_t handler)
{
    // Doesn't work for some reason! I nor Copilot can figure out why.
    // struct gpio_dt_spec spec = GPIO_DT_SPEC_GET_OR(DT_ALIAS(sw##i), gpios, { 0 });
    // So just hard code the pins for now
    struct gpio_dt_spec specs[MAX_BUTTONS] = {
        GPIO_DT_SPEC_GET_OR(DT_ALIAS(sw0), gpios, { 0 }),
        GPIO_DT_SPEC_GET_OR(DT_ALIAS(sw1), gpios, { 0 }),
        GPIO_DT_SPEC_GET_OR(DT_ALIAS(sw2), gpios, { 0 }),
        GPIO_DT_SPEC_GET_OR(DT_ALIAS(sw3), gpios, { 0 }),
    };

    // I see to do this in a loop just as the one above doesn't work.
    // TBD: Might be possible to do this with the DT_FOREACH_NODE macro.
    // See foreach macros https://docs.zephyrproject.org/apidoc/latest/group__devicetree-generic-foreach.html
    uint32_t codes[MAX_BUTTONS] = {
        DT_PROP(DT_ALIAS(sw0), zephyr_code),
        DT_PROP(DT_ALIAS(sw1), zephyr_code),
        DT_PROP(DT_ALIAS(sw2), zephyr_code),
        DT_PROP(DT_ALIAS(sw3), zephyr_code),
    };

    for (int i = 0; i < MAX_BUTTONS; i++)
    {
        if (specs[i].port)
        {
            LOG_INF("Initializing button %d on pin %d\n", i, specs[i].pin);
            button_init(i, &specs[i], codes[i], handler);
        }
    }
}
