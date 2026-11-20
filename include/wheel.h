#ifndef WHEELMQ_WHEEL_H
#define WHEELMQ_WHEEL_H

#include "queue.h"
#include <stdint.h>
#include <stddef.h>

/**
 * Total hierarchy levels in the timing wheel.
 * Follows the Linux kernel timer wheel architecture (TVR + 4x TVN).
 */
#define WHEEL_LEVELS 5

/**
 * Level 0 Bit count and size:
 * 8 bits = 256 slots covering 0 - 255 milliseconds at 1ms precision.
 */
#define WHEEL_BITS_0 8
#define WHEEL_SIZE_0 (1 << WHEEL_BITS_0) // 256 slots
#define WHEEL_MASK_0 (WHEEL_SIZE_0 - 1)

/**
 * Levels 1 through 4 Bit count and size:
 * 6 bits = 64 slots per level.
 * Level 1: covers ~16.384 seconds (256ms resolution per slot)
 * Level 2: covers ~17.47 minutes (16.384s resolution per slot)
 * Level 3: covers ~18.64 hours (17.47m resolution per slot)
 * Level 4: covers ~49.7 days (18.64h resolution per slot)
 */
#define WHEEL_BITS_N 6
#define WHEEL_SIZE_N (1 << WHEEL_BITS_N) // 64 slots
#define WHEEL_MASK_N (WHEEL_SIZE_N - 1)

/**
 * 5-Level Hierarchical Timing Wheel structure.
 */
typedef struct {
    msg_list_t wheel0[WHEEL_SIZE_0];    /**< Level 0 wheel: 256 slots (0 - 255 ms) */
    msg_list_t wheel1[WHEEL_SIZE_N];    /**< Level 1 wheel: 64 slots (~16.38 seconds) */
    msg_list_t wheel2[WHEEL_SIZE_N];    /**< Level 2 wheel: 64 slots (~17.47 minutes) */
    msg_list_t wheel3[WHEEL_SIZE_N];    /**< Level 3 wheel: 64 slots (~18.64 hours) */
    msg_list_t wheel4[WHEEL_SIZE_N];    /**< Level 4 wheel: 64 slots (~49.7 days) */

    uint64_t current_time_ms;           /**< Monotonic timestamp in ms tracked by the wheel */
    size_t total_timer_count;           /**< Total active timers registered across all levels */
} timing_wheel_t;

/**
 * Initialize the timing wheel with a starting base timestamp.
 *
 * @param wheel Pointer to timing_wheel_t to initialize.
 * @param now_ms Initial epoch timestamp in milliseconds.
 */
void wheel_init(timing_wheel_t *wheel, uint64_t now_ms);

/**
 * Clean up all pending timer messages across all wheel levels.
 *
 * @param wheel Pointer to timing_wheel_t to destroy.
 */
void wheel_destroy(timing_wheel_t *wheel);

/**
 * Schedule a message into the appropriate wheel level in O(1) time based on msg->scheduled_at_ms.
 *
 * @param wheel Pointer to timing_wheel_t.
 * @param msg Pointer to message_t to schedule.
 */
void wheel_add(timing_wheel_t *wheel, message_t *msg);

/**
 * Advance the timing wheel to now_ms. Cascades higher-level timers down as lower
 * levels wrap around, and collects all expired messages into expired_out.
 *
 * @param wheel Pointer to timing_wheel_t.
 * @param now_ms Target timestamp in milliseconds.
 * @param expired_out Output list where expired messages are placed.
 * @return Count of expired messages collected.
 */
size_t wheel_advance(timing_wheel_t *wheel, uint64_t now_ms, msg_list_t *expired_out);

#endif // WHEELMQ_WHEEL_H
