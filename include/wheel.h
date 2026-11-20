#ifndef WHEELMQ_WHEEL_H
#define WHEELMQ_WHEEL_H

#include "queue.h"
#include <stdint.h>
#include <stddef.h>

#define WHEEL_LEVELS 5
#define WHEEL_BITS_0 8
#define WHEEL_SIZE_0 (1 << WHEEL_BITS_0) // 256 slots
#define WHEEL_MASK_0 (WHEEL_SIZE_0 - 1)

#define WHEEL_BITS_N 6
#define WHEEL_SIZE_N (1 << WHEEL_BITS_N) // 64 slots
#define WHEEL_MASK_N (WHEEL_SIZE_N - 1)

typedef struct {
    msg_list_t wheel0[WHEEL_SIZE_0];
    msg_list_t wheel1[WHEEL_SIZE_N];
    msg_list_t wheel2[WHEEL_SIZE_N];
    msg_list_t wheel3[WHEEL_SIZE_N];
    msg_list_t wheel4[WHEEL_SIZE_N];

    uint64_t current_time_ms;
    size_t total_timer_count;
} timing_wheel_t;

// Initialize the hierarchical timing wheel with a base timestamp in ms
void wheel_init(timing_wheel_t *wheel, uint64_t now_ms);

// Clean up all pending messages in all wheels
void wheel_destroy(timing_wheel_t *wheel);

// Schedule a message into the wheel based on msg->scheduled_at_ms (O(1))
void wheel_add(timing_wheel_t *wheel, message_t *msg);

// Advance the wheel to now_ms. Any timers that expired between previous time
// and now_ms are collected into expired_out.
// Returns the count of expired messages collected.
size_t wheel_advance(timing_wheel_t *wheel, uint64_t now_ms, msg_list_t *expired_out);

#endif // WHEELMQ_WHEEL_H
