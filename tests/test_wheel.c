#include "wheel.h"
#include <stdio.h>
#include <assert.h>
#include <string.h>

int main(void) {
    printf("[TEST] Hierarchical Timing Wheel...\n");

    timing_wheel_t wheel;
    uint64_t base_time = 1000000ULL; // 1000.000s in ms
    wheel_init(&wheel, base_time);

    // Schedule 3 timers:
    // Timer 1: +50 ms (Wheel Level 0)
    // Timer 2: +500 ms (Wheel Level 1 - cascaded down)
    // Timer 3: +30000 ms (30 seconds, Wheel Level 2 - cascaded down)
    message_t *m1 = message_create(101, "fast-tasks", "task 1", 6, base_time + 50, 1);
    message_t *m2 = message_create(102, "medium-tasks", "task 2", 6, base_time + 500, 1);
    message_t *m3 = message_create(103, "slow-tasks", "task 3", 6, base_time + 30000, 1);

    wheel_add(&wheel, m1);
    wheel_add(&wheel, m2);
    wheel_add(&wheel, m3);
    assert(wheel.total_timer_count == 3);

    msg_list_t expired;
    memset(&expired, 0, sizeof(msg_list_t));

    // Advance 40ms -> nothing should fire
    size_t fired = wheel_advance(&wheel, base_time + 40, &expired);
    assert(fired == 0);
    assert(expired.count == 0);

    // Advance to 60ms -> Timer 1 should fire!
    fired = wheel_advance(&wheel, base_time + 60, &expired);
    assert(fired == 1);
    assert(expired.count == 1);
    assert(expired.head->id == 101);
    message_free(expired.head);
    memset(&expired, 0, sizeof(msg_list_t));
    printf("  [PASS] Level 0 timer expired precisely at +50ms\n");

    // Advance to 600ms -> Timer 2 should cascade from Level 1 and fire!
    fired = wheel_advance(&wheel, base_time + 600, &expired);
    assert(fired == 1);
    assert(expired.count == 1);
    assert(expired.head->id == 102);
    message_free(expired.head);
    memset(&expired, 0, sizeof(msg_list_t));
    printf("  [PASS] Level 1 timer cascaded and expired precisely at +500ms\n");

    // Advance to 35000ms -> Timer 3 should cascade from Level 2 and fire!
    fired = wheel_advance(&wheel, base_time + 35000, &expired);
    assert(fired == 1);
    assert(expired.count == 1);
    assert(expired.head->id == 103);
    message_free(expired.head);
    memset(&expired, 0, sizeof(msg_list_t));
    printf("  [PASS] Level 2 timer cascaded and expired precisely at +30000ms\n");

    assert(wheel.total_timer_count == 0);
    wheel_destroy(&wheel);

    printf("[SUCCESS] All Timing Wheel tests passed!\n");
    return 0;
}
