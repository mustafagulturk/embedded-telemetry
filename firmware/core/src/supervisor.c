#include "supervisor.h"

#include <stddef.h>
#include <string.h>

void supervisor_init(supervisor_t *s, uint32_t now_ms)
{
    if (s != NULL) {
        (void)memset(s, 0, sizeof(*s));
        for (uint32_t i = 0U; i < SUPERVISOR_MAX_TASKS; i++) {
            s->last_checkin_ms[i] = now_ms;
        }
    }
}

int32_t supervisor_register(supervisor_t *s, uint32_t deadline_ms, uint32_t now_ms)
{
    int32_t id;

    if ((s == NULL) || (deadline_ms == 0U) || (s->count >= SUPERVISOR_MAX_TASKS)) {
        return -1;
    }
    id = (int32_t)s->count;
    s->deadline_ms[s->count] = deadline_ms;
    s->last_checkin_ms[s->count] = now_ms;
    s->count++;
    return id;
}

void supervisor_checkin(supervisor_t *s, int32_t id, uint32_t now_ms)
{
    if ((s != NULL) && (id >= 0) && (id < (int32_t)s->count)) {
        s->last_checkin_ms[id] = now_ms;
    }
}

uint32_t supervisor_evaluate(const supervisor_t *s, uint32_t now_ms)
{
    uint32_t late = 0U;

    if (s == NULL) {
        return UINT32_MAX;
    }
    for (uint32_t i = 0U; i < s->count; i++) {
        /* Unsigned subtraction is wrap-safe for the 49-day ms counter. */
        if ((now_ms - s->last_checkin_ms[i]) > s->deadline_ms[i]) {
            late |= ((uint32_t)1U << i);
        }
    }
    return late;
}
