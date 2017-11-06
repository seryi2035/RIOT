/*
 * Copyright (C) 2017 Unwired Devices <info@unwds.com>
 *
 * This file is subject to the terms and conditions of the GNU Lesser
 * General Public License v2.1. See the file LICENSE in the top level
 * directory for more details.
 */

/**
 * @defgroup    
 * @ingroup     
 * @brief       
 * @{
 * @file
 * @brief       RTC subseconds timer core (not every device supports it)
 * @author      Oleg Artamonov <oleg@unwds.com>
 */

#ifdef __cplusplus
extern "C" {
#endif

#define USE_WKUP_TIMER

#include <stdio.h>
#include <stdlib.h>

#include "rtctimers-millis.h"
#include "periph/rtc.h"

#include "periph/gpio.h"

#include "debug.h"
#define ENABLE_DEBUG    (1)

static rtctimers_millis_t *timer_list_head = NULL;

static void _add_timer_millis_to_list(rtctimers_millis_t **list_head, rtctimers_millis_t *timer);
static void _remove(rtctimers_millis_t *timer);
static void _rtc_callback(void *arg);
static void _timer_callback(void);
static void _timers_recalculate_relative(void);

static void _lltimer_millis_set(uint32_t millis) {
	rtc_millis_set_alarm(millis, _rtc_callback, NULL);
}

static void _rtc_callback(void *arg) {
	_timer_callback();
}

static void _timers_recalculate_relative(void) {
    uint32_t time_ms = 0;
    rtc_millis_get_time_till_next_alarm(&time_ms);
    rtctimers_millis_t *list_head = timer_list_head;
    uint32_t time_passed = list_head->target - time_ms;
    
    /* all targets should be relative to current time, not absolute */
    while(list_head) {
        list_head->target -= time_passed;
        list_head = list_head->next;
    }
}

void rtctimers_millis_init(void) {
	rtc_millis_init();
}

int _rtctimers_millis_set_absolute(rtctimers_millis_t *timer, uint32_t target)
{
    int res = 0;
    timer->next = NULL;

    unsigned state = irq_disable();

    timer->target = target;
    _add_timer_millis_to_list(&timer_list_head, timer);

    if (timer_list_head == timer) {
        _lltimer_millis_set(target);
	}

    irq_restore(state);

    return res;
}

void rtctimers_millis_set(rtctimers_millis_t *timer, uint32_t offset) {
    if (!timer->callback) {
        DEBUG("timer_set(): timer has no callback.\n");
        return;
    }
    rtctimers_millis_remove(timer);

	_rtctimers_millis_set_absolute(timer, offset);
}

static void _add_timer_millis_to_list(rtctimers_millis_t **list_head, rtctimers_millis_t *timer)
{
    /* there's no list yet */
    if (!*list_head) {
        timer->next = NULL;
        *list_head = timer;
        return;
    }
    
    _timers_recalculate_relative();
    
    while(*list_head && (*list_head)->target <= timer->target) {
        list_head = &((*list_head)->next);
    }

    timer->next = *list_head;
    *list_head = timer;
}

static int _remove_timer_millis_from_list(rtctimers_millis_t **list_head, rtctimers_millis_t *timer)
{
    while (*list_head) {
        if (*list_head == timer) {
            *list_head = timer->next;
            return 1;
        }
        list_head = &((*list_head)->next);
    }

    return 0;
}

static void _remove(rtctimers_millis_t *timer)
{
    if (timer_list_head == timer) {
        uint32_t next;
        timer_list_head = timer->next;
        
        if (timer_list_head) {
            /* recalculate targets relative to current time */
            _timers_recalculate_relative();

            /* schedule callback on next timer target time */
            next = timer_list_head->target;
            _lltimer_millis_set(next);
        }
        else {
            rtc_millis_clear_alarm();
        }
    }
    else {
        if (!_remove_timer_millis_from_list(&timer_list_head, timer)) {
        	//DEBUG("[rtctimers] Unable to remove timer from list!\n");
        }
    }
}

void rtctimers_millis_remove(rtctimers_millis_t *timer) {
    int state = irq_disable();

    _remove(timer);
    
    irq_restore(state);
}

static inline void _shoot(rtctimers_millis_t *timer) {
	timer->callback(timer->arg);
}

static void _timer_callback(void)
{
    uint32_t next_target = 0;
    
    uint32_t time_ms = 0;
    rtc_millis_get_time_till_next_alarm(&time_ms);

    /* disable alarm or it may continue to fire */
    rtc_millis_clear_alarm();
    
    /* check if next timers are close to expiring */
    while (timer_list_head && (timer_list_head->target <= time_ms + RTCTIMERS_MILLIS_ISR_BACKOFF)) {
        /* pick first timer in list */
        
        rtctimers_millis_t *timer = timer_list_head;
        /* move list head to the next timer */
        _remove_timer_millis_from_list(&timer_list_head, timer);
        /* fire timer */
        _shoot(timer);
    }

    if (timer_list_head) {
        /* schedule callback on next timer target time */
        next_target = timer_list_head->target - RTCTIMERS_MILLIS_OVERHEAD;
    }

    /* set low level timer */
    if (next_target)
    	_lltimer_millis_set(next_target);
}

#ifdef __cplusplus
}
#endif