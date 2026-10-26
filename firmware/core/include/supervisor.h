/**
 * @file supervisor.h
 * @brief Task liveness supervision that gates the hardware watchdog refresh.
 *
 * Every supervised task calls supervisor_checkin() each cycle. The watchdog
 * task calls supervisor_evaluate(); the IWDG is refreshed only when no task has
 * been silent for longer than its deadline. A stuck task therefore results in
 * a watchdog reset instead of a firmware that silently stopped working.
 */
#ifndef SUPERVISOR_H
#define SUPERVISOR_H

#include <stdbool.h>
#include <stdint.h>

#define SUPERVISOR_MAX_TASKS 8U

typedef struct {
    uint32_t deadline_ms[SUPERVISOR_MAX_TASKS];
    volatile uint32_t last_checkin_ms[SUPERVISOR_MAX_TASKS];
    uint8_t count;
} supervisor_t;

void supervisor_init(supervisor_t *s, uint32_t now_ms);

/** Register a task; returns its id, or -1 if full / bad deadline. */
int32_t supervisor_register(supervisor_t *s, uint32_t deadline_ms, uint32_t now_ms);

void supervisor_checkin(supervisor_t *s, int32_t id, uint32_t now_ms);

/** Bitmask of overdue tasks (bit n = task id n). 0 means "safe to kick". */
uint32_t supervisor_evaluate(const supervisor_t *s, uint32_t now_ms);

#endif /* SUPERVISOR_H */
