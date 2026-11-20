#ifndef WHEELMQ_QUEUE_H
#define WHEELMQ_QUEUE_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

/**
 * ============================================================================
 * ARCHITECTURAL DESIGN DECISIONS & THEORETICAL FOUNDATION: QUEUE & DLQ
 * ============================================================================
 *
 * 1. WHY INTRUSIVE DOUBLY LINKED LISTS:
 *    - Strict O(1) Push and Pop: Inserting to the tail and popping from the head
 *      require only pointer adjustments without array reallocation or shifting.
 *    - Strict O(1) Arbitrary Removal: When a specific message in the middle of
 *      the list needs to be canceled, evicted, or replayed from DLQ, an intrusive
 *      doubly linked list enables instantaneous node unlinking without searching
 *      or iterating (msg->prev->next = msg->next).
 *
 * 2. DEAD LETTER QUEUE (DLQ) & RETRY POLICY (max_retries = 3):
 *    - Origin: Standardized across enterprise message brokers (AWS SQS,
 *      RabbitMQ, Apache ActiveMQ).
 *    - Transient vs Poison Pill Fault Isolation: Statistical telemetry reveals
 *      that transient network glitches and socket resets resolve within 1-2 retries.
 *      Failures persisting past 3 attempts almost universally indicate "poison pills"
 *      (e.g., malformed payloads, database schema mismatches, hard down dependencies).
 *      Escalating to the DLQ prevents poison pills from creating infinite loops
 *      and consuming 100% of CPU worker capacity.
 *
 * 3. ZERO-COPY IN-MEMORY HOT PATH:
 *    - All message descriptors are allocated with contiguous byte buffers.
 *    - During queue transitions (Timing Wheel -> Active FIFO -> Worker Pop),
 *      the payload is never re-copied; only the struct message_s pointers
 *      are transferred between linked list heads and tails.
 * ============================================================================
 */

/**
 * Maximum length for a queue name.
 */
#define QUEUE_NAME_MAX 64

/**
 * Maximum length for a failure reason description stored in DLQ.
 */
#define ERROR_MSG_MAX 128

/**
 * Internal representation of a message or job in wheelmq.
 */
typedef struct message_s {
    uint64_t id;                        /**< Unique auto-incrementing message identifier */
    char queue_name[QUEUE_NAME_MAX];    /**< Destination queue name */
    uint8_t *payload;                   /**< Heap-allocated message payload buffer */
    size_t payload_len;                 /**< Payload size in bytes */

    uint64_t created_at_ms;             /**< Epoch timestamp (ms) when message was created */
    uint64_t scheduled_at_ms;           /**< Epoch timestamp (ms) when message becomes ready */
    uint32_t retry_count;               /**< Number of failed delivery attempts so far */
    uint32_t max_retries;               /**< Maximum retry threshold before moving to DLQ */

    char error_msg[ERROR_MSG_MAX];      /**< Root cause / failure reason if placed in DLQ */

    struct message_s *next;             /**< Pointer to next message in doubly linked list */
    struct message_s *prev;             /**< Pointer to previous message in doubly linked list */
} message_t;

/**
 * Doubly linked list of messages with O(1) push and pop operations.
 */
typedef struct {
    message_t *head;                    /**< Head pointer (oldest message) */
    message_t *tail;                    /**< Tail pointer (newest message) */
    size_t count;                       /**< Number of messages in this list */
} msg_list_t;

/**
 * Logical queue container consisting of an active FIFO list and a DLQ.
 */
typedef struct {
    char name[QUEUE_NAME_MAX];          /**< Name of the queue */
    msg_list_t ready_fifo;              /**< List of messages ready for immediate worker pop */
    msg_list_t dlq;                     /**< Dead Letter Queue containing permanently failed messages */
} queue_t;

/**
 * Allocate and initialize a new message.
 *
 * @param id Unique message ID.
 * @param queue_name Target queue name.
 * @param payload Raw payload byte buffer.
 * @param payload_len Size of payload in bytes.
 * @param scheduled_at_ms Target execution time in epoch ms (0 = immediate).
 * @param max_retries Maximum retry attempts before DLQ escalation.
 * @return Pointer to newly allocated message_t, or NULL if out of memory.
 */
message_t *message_create(uint64_t id, const char *queue_name,
                          const void *payload, size_t payload_len,
                          uint64_t scheduled_at_ms, uint32_t max_retries);

/**
 * Free a message and its internal payload buffer.
 *
 * @param msg Pointer to message_t to free.
 */
void message_free(message_t *msg);

/**
 * Initialize an empty queue structure.
 *
 * @param q Pointer to queue_t to initialize.
 * @param name Queue name identifier.
 */
void queue_init(queue_t *q, const char *name);

/**
 * Destroy a queue and free all messages in its active FIFO and DLQ lists.
 *
 * @param q Pointer to queue_t to destroy.
 */
void queue_destroy(queue_t *q);

/**
 * Enqueue a message to the back of the ready FIFO list in O(1) time.
 *
 * @param q Target queue pointer.
 * @param msg Message to enqueue.
 */
void queue_enqueue_ready(queue_t *q, message_t *msg);

/**
 * Dequeue the oldest message from the front of the ready FIFO list in O(1) time.
 *
 * @param q Target queue pointer.
 * @return Pointer to message_t if available, or NULL if queue is empty.
 */
message_t *queue_dequeue_ready(queue_t *q);

/**
 * Move a message to the Dead Letter Queue (DLQ) with an error description.
 *
 * @param q Target queue pointer.
 * @param msg Message that failed execution.
 * @param reason Human-readable failure reason string.
 */
void queue_move_to_dlq(queue_t *q, message_t *msg, const char *reason);

/**
 * Replay a message from DLQ back to the active ready FIFO queue.
 *
 * @param q Target queue pointer.
 * @param msg_id ID of the dead-lettered message to replay.
 * @return true if message was found and replayed, false otherwise.
 */
bool queue_replay_dlq(queue_t *q, uint64_t msg_id);

/**
 * Return current number of messages in the active ready FIFO list.
 *
 * @param q Target queue pointer.
 * @return Active message count.
 */
size_t queue_ready_count(const queue_t *q);

/**
 * Return current number of messages in the Dead Letter Queue.
 *
 * @param q Target queue pointer.
 * @return Dead letter message count.
 */
size_t queue_dlq_count(const queue_t *q);

#endif // WHEELMQ_QUEUE_H
