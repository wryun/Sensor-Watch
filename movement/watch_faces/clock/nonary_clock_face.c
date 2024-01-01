/*
 * MIT License
 *
 * Copyright (c) 2024 James Haggerty <james@gruemail.com>
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#include <stdlib.h>
#include "nonary_clock_face.h"
#include "watch.h"
#include "watch_utility.h"
#include "watch_private_display.h"

static void _update_alarm_indicator(bool settings_alarm_enabled, nonary_clock_state_t *state) {
    state->alarm_enabled = settings_alarm_enabled;
    if (state->alarm_enabled) watch_set_indicator(WATCH_INDICATOR_SIGNAL);
    else watch_clear_indicator(WATCH_INDICATOR_SIGNAL);
}

void nonary_clock_face_setup(movement_settings_t *settings, uint8_t watch_face_index, void ** context_ptr) {
    (void) settings;
    (void) watch_face_index;

    if (*context_ptr == NULL) {
        *context_ptr = malloc(sizeof(nonary_clock_state_t));
        nonary_clock_state_t *state = (nonary_clock_state_t *)*context_ptr;
        state->signal_enabled = false;
        state->watch_face_index = watch_face_index;
    }
}

void nonary_clock_face_activate(movement_settings_t *settings, void *context) {
    nonary_clock_state_t *state = (nonary_clock_state_t *)context;

    if (watch_tick_animation_is_running()) watch_stop_tick_animation();

    if (settings->bit.clock_mode_24h) watch_set_indicator(WATCH_INDICATOR_24H);

    // handle chime indicator
    if (state->signal_enabled) watch_set_indicator(WATCH_INDICATOR_BELL);
    else watch_clear_indicator(WATCH_INDICATOR_BELL);

    // show alarm indicator if there is an active alarm
    _update_alarm_indicator(settings->bit.alarm_enabled, state);

    watch_set_colon();

    // this ensures that none of the timestamp fields will match, so we can re-render them all.
    state->previous_date_time = 0xFFFFFFFF;
}

char nonary_digit_map[] = {
    'd',
    'c',
    'b',
    'a',
    '0',
    '1',
    '2',
    '3',
    '4',
};

static void write_as_nonary(int32_t val, char *buf, int max_len) {
    int sign = val >= 0 ? 1 : -1;
    val = abs(val);
    for (int i = 0; i < max_len; ++i) {
        int digit = val % 9;
        val /= 9;
        if (digit >= 5) {
            digit -= 9;
            val += 1;
        }
        *(buf--) = nonary_digit_map[digit * sign + 4];
    }
}

static const int32_t NONARY_SECS_IN_HOUR = 9 * 9 * 9 * 9;

bool nonary_clock_face_loop(movement_event_t event, movement_settings_t *settings, void *context) {
    nonary_clock_state_t *state = (nonary_clock_state_t *)context;
    char buf[11];

    watch_date_time date_time;
    uint32_t previous_date_time;
    switch (event.event_type) {
        case EVENT_ACTIVATE:
        case EVENT_TICK:
        case EVENT_LOW_ENERGY_UPDATE:
            date_time = watch_rtc_get_date_time();
            previous_date_time = state->previous_date_time;
            state->previous_date_time = date_time.reg;

            // check the battery voltage once a day...
            if (date_time.unit.day != state->last_battery_check) {
                state->last_battery_check = date_time.unit.day;
                watch_enable_adc();
                uint16_t voltage = watch_get_vcc_voltage();
                watch_disable_adc();
                // 2.2 volts will happen when the battery has maybe 5-10% remaining?
                // we can refine this later.
                state->battery_low = (voltage < 2200);
            }

            // ...and set the LAP indicator if low.
            if (state->battery_low) watch_set_indicator(WATCH_INDICATOR_LAP);

            int32_t nonary_secs_past_hour = (date_time.unit.minute * 60 + date_time.unit.second) * NONARY_SECS_IN_HOUR / 3600;
            int32_t nonary_secs_from_hour = nonary_secs_past_hour <= NONARY_SECS_IN_HOUR / 2 ? nonary_secs_past_hour : (nonary_secs_past_hour - NONARY_SECS_IN_HOUR);
            write_as_nonary(nonary_secs_from_hour, &buf[9], 4);
            buf[10] = '\0';

            if ((date_time.reg >> 12) == (previous_date_time >> 12) && event.event_type != EVENT_LOW_ENERGY_UPDATE) {
                // everything before minutes is the same.
                watch_display_string(&buf[6], 6);
            } else {
                sprintf(buf, "%s%2d", watch_utility_get_weekday(date_time), date_time.unit.day);
                int32_t nonary_hour = (int32_t)date_time.unit.hour - 12 + (nonary_secs_from_hour < 0);
                write_as_nonary(nonary_hour, &buf[5], 2);
                if (event.event_type == EVENT_LOW_ENERGY_UPDATE) {
                    if (!watch_tick_animation_is_running()) watch_start_tick_animation(500);
                    buf[8] = buf[9] = ' ';
                }
                watch_display_string(buf, 0);
            }
            // handle alarm indicator
            if (state->alarm_enabled != settings->bit.alarm_enabled) _update_alarm_indicator(settings->bit.alarm_enabled, state);
            break;
        case EVENT_ALARM_LONG_PRESS:
            state->signal_enabled = !state->signal_enabled;
            if (state->signal_enabled) watch_set_indicator(WATCH_INDICATOR_BELL);
            else watch_clear_indicator(WATCH_INDICATOR_BELL);
            break;
        case EVENT_BACKGROUND_TASK:
            // uncomment this line to snap back to the clock face when the hour signal sounds:
            // movement_move_to_face(state->watch_face_index);
            #ifdef SIGNAL_TUNE_DEFAULT
            movement_play_signal();
            #else
            //movement_play_tune();
            #endif
            break;
        default:
            return movement_default_loop_handler(event, settings);
    }

    return true;
}

void nonary_clock_face_resign(movement_settings_t *settings, void *context) {
    (void) settings;
    (void) context;
}

bool nonary_clock_face_wants_background_task(movement_settings_t *settings, void *context) {
    (void) settings;
    nonary_clock_state_t *state = (nonary_clock_state_t *)context;
    if (!state->signal_enabled) return false;

    watch_date_time date_time = watch_rtc_get_date_time();

    return date_time.unit.minute == 0;
}
