#ifndef BUTTON_H
#define BUTTON_H
#define BTN_PATTERN 0
#define BTN_RIGHT   1
#define BTN_LEFT    2
#define BTN_HAZARD  3

#include <zephyr/drivers/gpio.h>

enum button_evt
{
    BUTTON_EVT_PRESSED,
    BUTTON_EVT_RELEASED,
};

typedef void (*button_event_handler_t)(enum button_evt evt, uint32_t code);

int button_init(int index, const struct gpio_dt_spec* spec, uint32_t code, button_event_handler_t handler);
void buttons_init(button_event_handler_t handler);

#endif // BUTTON_H
