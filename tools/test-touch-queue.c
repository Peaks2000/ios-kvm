#include <assert.h>
#include <stdio.h>
#include "hakios.touch_queue.h"

#define CAPACITY 8
static struct hakios_touch queue[CAPACITY];
static uint32_t head, tail;

static void post(uint32_t type, uint32_t finger, uint32_t value)
{
    struct hakios_touch *at = hakios_touch_queue_reserve(queue, &head, &tail, CAPACITY, type, finger);
    *at = (struct hakios_touch){.type=type, .finger=finger, .x=value, .y=value+1, .time=value};
    assert(tail-head <= CAPACITY);
}

static void take(uint32_t type, uint32_t finger, uint32_t value)
{
    assert(head != tail);
    struct hakios_touch at = queue[head++ % CAPACITY];
    assert(at.type == type && at.finger == finger && at.x == value && at.time == value);
}

int main(void)
{
    post(HAKIOS_TOUCH_DOWN,0,0);
    for (unsigned i=1;i<=1000;i++) post(HAKIOS_TOUCH_MOVE,0,i);
    post(HAKIOS_TOUCH_UP,0,1001);
    assert(tail-head == 3);
    take(HAKIOS_TOUCH_DOWN,0,0);
    take(HAKIOS_TOUCH_MOVE,0,1000);
    take(HAKIOS_TOUCH_UP,0,1001);
    assert(head == tail);

    head=tail=UINT32_MAX-2;
    for (unsigned i=0;i<100;i++) {
        post(HAKIOS_TOUCH_DOWN,0,1);
        post(HAKIOS_TOUCH_MOVE,0,2);
        post(HAKIOS_TOUCH_MOVE,0,3);
        post(HAKIOS_TOUCH_MOVE,1,4);
        post(HAKIOS_TOUCH_MOVE,0,5);
        post(HAKIOS_TOUCH_UP,0,6);
        post(HAKIOS_BUTTON_DOWN,4,7);
        post(HAKIOS_BUTTON_UP,4,8);
        post(HAKIOS_TOUCH_DOWN,0,9);
        assert(tail-head == CAPACITY);
        take(HAKIOS_TOUCH_DOWN,0,1);
        take(HAKIOS_TOUCH_MOVE,0,3);
        take(HAKIOS_TOUCH_MOVE,1,4);
        take(HAKIOS_TOUCH_MOVE,0,5);
        take(HAKIOS_TOUCH_UP,0,6);
        take(HAKIOS_BUTTON_DOWN,4,7);
        take(HAKIOS_BUTTON_UP,4,8);
        take(HAKIOS_TOUCH_DOWN,0,9);
        assert(head == tail);
    }
    /* Existing overflow policy stays bounded when no movements can merge. */
    for (unsigned i=0;i<CAPACITY+1;i++) post(HAKIOS_BUTTON_DOWN,4,i);
    for (unsigned i=1;i<CAPACITY+1;i++) take(HAKIOS_BUTTON_DOWN,4,i);
    assert(head == tail);
    puts("touch queue: latest move, edges, fingers, wraparound and bounded overflow passed");
    return 0;
}
