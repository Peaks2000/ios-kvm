/* Keep pending pointer movement current without crossing a touch/button edge. */
#ifndef HAKIOS_TOUCH_QUEUE_H
#define HAKIOS_TOUCH_QUEUE_H

#include <sys/hakios.input.h>

static inline struct hakios_touch *
hakios_touch_queue_reserve(struct hakios_touch *queue, uint32_t *head, uint32_t *tail,
    uint32_t capacity, uint32_t type, uint32_t finger)
{
    if (*tail != *head && type == HAKIOS_TOUCH_MOVE) {
        struct hakios_touch *previous = &queue[(*tail - 1) % capacity];

        if (previous->type == HAKIOS_TOUCH_MOVE && previous->finger == finger) {
            return previous;
        }
    }
    if (*tail - *head == capacity) {
        (*head)++; /* Preserve the existing bounded-queue overflow policy. */
    }
    return &queue[(*tail)++ % capacity];
}

#endif
