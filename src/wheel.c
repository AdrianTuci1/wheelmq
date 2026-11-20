#include "wheel.h"
#include <string.h>
#include <stdio.h>

static void list_push(msg_list_t *list, message_t *msg) {
    msg->next = NULL;
    msg->prev = list->tail;
    if (list->tail) {
        list->tail->next = msg;
    } else {
        list->head = msg;
    }
    list->tail = msg;
    list->count++;
}

static message_t *list_pop(msg_list_t *list) {
    if (!list->head) return NULL;
    message_t *msg = list->head;
    list->head = msg->next;
    if (list->head) {
        list->head->prev = NULL;
    } else {
        list->tail = NULL;
    }
    msg->next = NULL;
    msg->prev = NULL;
    list->count--;
    return msg;
}

void wheel_init(timing_wheel_t *wheel, uint64_t now_ms) {
    memset(wheel, 0, sizeof(timing_wheel_t));
    wheel->current_time_ms = now_ms;
}

void wheel_destroy(timing_wheel_t *wheel) {
    for (int i = 0; i < WHEEL_SIZE_0; i++) {
        while (wheel->wheel0[i].head) {
            message_t *m = list_pop(&wheel->wheel0[i]);
            message_free(m);
        }
    }
    for (int i = 0; i < WHEEL_SIZE_N; i++) {
        while (wheel->wheel1[i].head) {
            message_t *m = list_pop(&wheel->wheel1[i]);
            message_free(m);
        }
        while (wheel->wheel2[i].head) {
            message_t *m = list_pop(&wheel->wheel2[i]);
            message_free(m);
        }
        while (wheel->wheel3[i].head) {
            message_t *m = list_pop(&wheel->wheel3[i]);
            message_free(m);
        }
        while (wheel->wheel4[i].head) {
            message_t *m = list_pop(&wheel->wheel4[i]);
            message_free(m);
        }
    }
    wheel->total_timer_count = 0;
}

static void internal_add(timing_wheel_t *wheel, message_t *msg) {
    uint64_t expires = msg->scheduled_at_ms;

    if (expires <= wheel->current_time_ms) {
        // Due immediately, place in current slot of wheel 0
        uint32_t idx = wheel->current_time_ms & WHEEL_MASK_0;
        list_push(&wheel->wheel0[idx], msg);
        return;
    }

    uint64_t delta = expires - wheel->current_time_ms;

    if (delta < (1ULL << 8)) {
        uint32_t idx = expires & WHEEL_MASK_0;
        list_push(&wheel->wheel0[idx], msg);
    } else if (delta < (1ULL << 14)) {
        uint32_t idx = (expires >> 8) & WHEEL_MASK_N;
        list_push(&wheel->wheel1[idx], msg);
    } else if (delta < (1ULL << 20)) {
        uint32_t idx = (expires >> 14) & WHEEL_MASK_N;
        list_push(&wheel->wheel2[idx], msg);
    } else if (delta < (1ULL << 26)) {
        uint32_t idx = (expires >> 20) & WHEEL_MASK_N;
        list_push(&wheel->wheel3[idx], msg);
    } else {
        uint32_t idx = (expires >> 26) & WHEEL_MASK_N;
        list_push(&wheel->wheel4[idx], msg);
    }
}

void wheel_add(timing_wheel_t *wheel, message_t *msg) {
    internal_add(wheel, msg);
    wheel->total_timer_count++;
}

static void cascade(timing_wheel_t *wheel, msg_list_t *source_slot) {
    message_t *msg = list_pop(source_slot);
    while (msg) {
        internal_add(wheel, msg);
        msg = list_pop(source_slot);
    }
}

size_t wheel_advance(timing_wheel_t *wheel, uint64_t now_ms, msg_list_t *expired_out) {
    if (now_ms <= wheel->current_time_ms) {
        return 0;
    }

    size_t expired_count = 0;

    while (wheel->current_time_ms < now_ms) {
        wheel->current_time_ms++;

        uint32_t idx0 = wheel->current_time_ms & WHEEL_MASK_0;

        // When wheel0 wraps to 0, cascade from wheel1
        if (idx0 == 0) {
            uint32_t idx1 = (wheel->current_time_ms >> 8) & WHEEL_MASK_N;
            cascade(wheel, &wheel->wheel1[idx1]);

            // When wheel1 wraps, cascade from wheel2
            if (idx1 == 0) {
                uint32_t idx2 = (wheel->current_time_ms >> 14) & WHEEL_MASK_N;
                cascade(wheel, &wheel->wheel2[idx2]);

                if (idx2 == 0) {
                    uint32_t idx3 = (wheel->current_time_ms >> 20) & WHEEL_MASK_N;
                    cascade(wheel, &wheel->wheel3[idx3]);

                    if (idx3 == 0) {
                        uint32_t idx4 = (wheel->current_time_ms >> 26) & WHEEL_MASK_N;
                        cascade(wheel, &wheel->wheel4[idx4]);
                    }
                }
            }
        }

        // Collect all timers in wheel0[idx0]
        message_t *msg = list_pop(&wheel->wheel0[idx0]);
        while (msg) {
            list_push(expired_out, msg);
            if (wheel->total_timer_count > 0) {
                wheel->total_timer_count--;
            }
            expired_count++;
            msg = list_pop(&wheel->wheel0[idx0]);
        }
    }

    return expired_count;
}
