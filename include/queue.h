#ifndef WHEELMQ_QUEUE_H
#define WHEELMQ_QUEUE_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#define QUEUE_NAME_MAX 64
#define ERROR_MSG_MAX 128

typedef struct message_s {
    uint64_t id;
    char queue_name[QUEUE_NAME_MAX];
    uint8_t *payload;
    size_t payload_len;

    uint64_t created_at_ms;
    uint64_t scheduled_at_ms;
    uint32_t retry_count;
    uint32_t max_retries;

    char error_msg[ERROR_MSG_MAX];

    struct message_s *next;
    struct message_s *prev;
} message_t;

typedef struct {
    message_t *head;
    message_t *tail;
    size_t count;
} msg_list_t;

typedef struct {
    char name[QUEUE_NAME_MAX];
    msg_list_t ready_fifo; // Ready to be consumed immediately
    msg_list_t dlq;        // Dead letter queue
} queue_t;

// Create a new heap-allocated message
message_t *message_create(uint64_t id, const char *queue_name,
                          const void *payload, size_t payload_len,
                          uint64_t scheduled_at_ms, uint32_t max_retries);

// Free a message and its payload
void message_free(message_t *msg);

// Initialize a queue
void queue_init(queue_t *q, const char *name);

// Clear and free all messages in a queue (both FIFO and DLQ)
void queue_destroy(queue_t *q);

// Enqueue message to FIFO (O(1))
void queue_enqueue_ready(queue_t *q, message_t *msg);

// Dequeue oldest message from FIFO (O(1))
message_t *queue_dequeue_ready(queue_t *q);

// Move failed message to DLQ
void queue_move_to_dlq(queue_t *q, message_t *msg, const char *reason);

// Replay a message from DLQ back to ready FIFO
bool queue_replay_dlq(queue_t *q, uint64_t msg_id);

// Peek FIFO count and DLQ count
size_t queue_ready_count(const queue_t *q);
size_t queue_dlq_count(const queue_t *q);

#endif // WHEELMQ_QUEUE_H
