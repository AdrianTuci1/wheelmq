#include "queue.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <time.h>

/**
 * Return current wall-clock epoch timestamp in milliseconds.
 */
static uint64_t get_current_time_ms(void) {
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    return (uint64_t)ts.tv_sec * 1000ULL + (uint64_t)ts.tv_nsec / 1000000ULL;
}

message_t *message_create(uint64_t id, const char *queue_name,
                          const void *payload, size_t payload_len,
                          uint64_t scheduled_at_ms, uint32_t max_retries) {
    message_t *msg = (message_t *)calloc(1, sizeof(message_t));
    if (!msg) return NULL;

    msg->id = id;
    if (queue_name) {
        strncpy(msg->queue_name, queue_name, sizeof(msg->queue_name) - 1);
    }
    if (payload && payload_len > 0) {
        msg->payload = (uint8_t *)malloc(payload_len);
        if (!msg->payload) {
            free(msg);
            return NULL;
        }
        memcpy(msg->payload, payload, payload_len);
        msg->payload_len = payload_len;
    }

    msg->created_at_ms = get_current_time_ms();
    msg->scheduled_at_ms = scheduled_at_ms ? scheduled_at_ms : msg->created_at_ms;
    msg->max_retries = max_retries;
    msg->retry_count = 0;

    return msg;
}

void message_free(message_t *msg) {
    if (!msg) return;
    if (msg->payload) {
        free(msg->payload);
        msg->payload = NULL;
    }
    free(msg);
}

/**
 * Append message to the tail of a doubly linked list in O(1).
 */
static void list_push_back(msg_list_t *list, message_t *msg) {
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

/**
 * Pop message from the head of a doubly linked list in O(1).
 */
static message_t *list_pop_front(msg_list_t *list) {
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

/**
 * Remove an arbitrary message from a doubly linked list in O(1).
 */
static void list_remove(msg_list_t *list, message_t *msg) {
    if (msg->prev) {
        msg->prev->next = msg->next;
    } else {
        list->head = msg->next;
    }

    if (msg->next) {
        msg->next->prev = msg->prev;
    } else {
        list->tail = msg->prev;
    }

    msg->next = NULL;
    msg->prev = NULL;
    list->count--;
}

/**
 * Free all messages in a doubly linked list.
 */
static void list_clear(msg_list_t *list) {
    message_t *curr = list->head;
    while (curr) {
        message_t *next = curr->next;
        message_free(curr);
        curr = next;
    }
    list->head = NULL;
    list->tail = NULL;
    list->count = 0;
}

void queue_init(queue_t *q, const char *name) {
    memset(q, 0, sizeof(queue_t));
    if (name) {
        strncpy(q->name, name, sizeof(q->name) - 1);
    }
}

void queue_destroy(queue_t *q) {
    list_clear(&q->ready_fifo);
    list_clear(&q->dlq);
}

void queue_enqueue_ready(queue_t *q, message_t *msg) {
    list_push_back(&q->ready_fifo, msg);
}

message_t *queue_dequeue_ready(queue_t *q) {
    return list_pop_front(&q->ready_fifo);
}

void queue_move_to_dlq(queue_t *q, message_t *msg, const char *reason) {
    if (reason) {
        strncpy(msg->error_msg, reason, sizeof(msg->error_msg) - 1);
        msg->error_msg[sizeof(msg->error_msg) - 1] = '\0';
    }
    list_push_back(&q->dlq, msg);
}

bool queue_replay_dlq(queue_t *q, uint64_t msg_id) {
    message_t *curr = q->dlq.head;
    while (curr) {
        if (curr->id == msg_id) {
            list_remove(&q->dlq, curr);
            curr->retry_count = 0;
            curr->error_msg[0] = '\0';
            list_push_back(&q->ready_fifo, curr);
            return true;
        }
        curr = curr->next;
    }
    return false;
}

size_t queue_ready_count(const queue_t *q) {
    return q->ready_fifo.count;
}

size_t queue_dlq_count(const queue_t *q) {
    return q->dlq.count;
}
