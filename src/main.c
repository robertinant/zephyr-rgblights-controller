/*
 * Copyright (c) 2019 Robert Wessels
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "button.h"
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(bike_light, LOG_LEVEL_INF);

#define INDICATOR_LEFT   0U
#define INDICATOR_RIGHT  1U
#define INDICATOR_OFF    2U
#define INDICATOR_HAZARD 3U

extern void rgbled_pattern_next(void);
extern void rgbled_left_right_hazard(uint8_t state);
static char* helper_button_evt_str(enum button_evt evt)
{
    switch (evt)
    {
    case BUTTON_EVT_PRESSED:
        return "Pressed";
    case BUTTON_EVT_RELEASED:
        return "Released";
    default:
        return "Unknown";
    }
}

static void button_event_handler(enum button_evt evt, uint32_t code)
{
    static bool hazard_state = 0;

    LOG_INF("Button event: %s, code: %d\n", helper_button_evt_str(evt), code);

    if (evt == BUTTON_EVT_PRESSED || evt == BUTTON_EVT_RELEASED)
    {
        switch (code)
        {
        case BTN_PATTERN:
            if (evt == BUTTON_EVT_PRESSED)
            {
                rgbled_pattern_next();
            }
            break;
        case BTN_LEFT:
            if (evt == BUTTON_EVT_PRESSED)
            {
                rgbled_left_right_hazard(INDICATOR_LEFT);
            }
            else if (evt == BUTTON_EVT_RELEASED)
            {
                rgbled_left_right_hazard(INDICATOR_OFF);
            }
            break;
        case BTN_RIGHT:
            if (evt == BUTTON_EVT_PRESSED)
            {
                rgbled_left_right_hazard(INDICATOR_RIGHT);
            }
            else if (evt == BUTTON_EVT_RELEASED)
            {
                rgbled_left_right_hazard(INDICATOR_OFF);
            }
            break;
        case BTN_HAZARD:
            if (evt == BUTTON_EVT_PRESSED)
            {
                hazard_state = !hazard_state;
                rgbled_left_right_hazard(hazard_state ? INDICATOR_HAZARD : INDICATOR_OFF);
            }
            break;
        default:
            break;
        }
    }
}

int main(void)
{
    LOG_INF("Main ran successfully");
    buttons_init(button_event_handler);
    return 0;
}